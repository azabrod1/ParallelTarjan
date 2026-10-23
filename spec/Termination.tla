----------------------------- MODULE Termination -----------------------------
(***************************************************************************)
(* Termination protocol of the parallel Tarjan scheduler at git b9d4ba4    *)
(* ("activeSearches" counter).                                             *)
(*                                                                         *)
(* Code mapped (paths relative to "Tarjan Code/" at b9d4ba4):              *)
(*   worker.cpp            Worker::operator()  getSearch -> execute ->     *)
(*                                             searchStopped               *)
(*   multiThreadedTarjan.cpp  MultiThreadedTarjan::getSearch               *)
(*   multiThreadedTarjan.hpp  searchStopped(), resumeAllBlockedOn()        *)
(*   stealingQueue.hpp/.cpp   isEmpty(): index.load() >= ttlCells          *)
(*                            next():    index++ loop                      *)
(*                                                                         *)
(* Every atomic access to shared state (pending.get / addPending,          *)
(* index.load, index++, activeSearches +=/-=/load, claimOrFail CAS) is its *)
(* own step.  All of them are seq_cst in the code, so an interleaving      *)
(* semantics is faithful.                                                  *)
(*                                                                         *)
(* ABSTRACTIONS (see README.md for the full list):                         *)
(*  A1 Searches are identified by the root index that started them         *)
(*     (search i = the search created by a successful claim of root i).    *)
(*     Search object recycling is not modelled (irrelevant to the count).  *)
(*  A2 Whether a root is still NEW (next()) and whether claimOrFail wins   *)
(*     are nondeterministic: any outcome the real graph could produce is   *)
(*     allowed.                                                            *)
(*  A3 execute() is abstracted to: finish, suspend on another live search, *)
(*     resolve a blocking cycle (stay running), or resume a non-empty      *)
(*     subset of the searches suspended on it.  The suspend() handshake    *)
(*     and cycle detection are atomic here; they are modelled at atomic    *)
(*     granularity in Suspension.tla and CycleDetection.tla.               *)
(*  A4 Pending is a set (any dequeue order), a superset of FIFO behaviour. *)
(*     addPending enqueues one search per step (lock-free queue); the      *)
(*     locked queue's single critical section is one of these schedules.   *)
(*  A5 MaxSusp bounds the total number of suspensions (the real graph is   *)
(*     finite, so the real number is finite too).                          *)
(*  A6 The 1 ms sleep is one step.  A worker may STALL forever in it when  *)
(*     AllowStall = TRUE (models a descheduled idle thread).               *)
(*                                                                         *)
(* Mutation switches (all FALSE/TRUE as in the real code by default) are   *)
(* used only to show that the properties are not vacuous:                  *)
(*   IncBeforeEnqueue = TRUE  : resumeAllBlockedOn does fetch_add(k) then  *)
(*                              addPending (real code).  FALSE swaps them. *)
(*   IncBeforeTake    = TRUE  : getSearch counts itself before             *)
(*                              cellQueue.next (real code).  FALSE swaps.  *)
(***************************************************************************)
EXTENDS Integers, FiniteSets, TLC

CONSTANTS Workers,          \* set of worker ids
          NRoots,           \* number of root cells in the stealing queue
          MaxSusp,          \* bound on total suspensions (A5)
          AllowStall,       \* may an idle worker stall forever in sleep_for?
          MaxStalls,        \* at most this many workers stall
          IncBeforeEnqueue, \* TRUE = real code
          IncBeforeTake,    \* TRUE = real code
          SpuriousEmpty     \* TRUE: pending.get() may return nullptr although
                            \* Pending is non-empty (moodycamel try_dequeue:
                            \* "the queue is likely but not guaranteed to be
                            \* empty"); FALSE: LockedPendingQueue

ASSUME NRoots \in Nat /\ MaxSusp \in Nat /\ MaxStalls \in Nat
ASSUME AllowStall \in BOOLEAN /\ IncBeforeEnqueue \in BOOLEAN /\ IncBeforeTake \in BOOLEAN
ASSUME SpuriousEmpty \in BOOLEAN

Searches == 1..NRoots
None     == 0

VARIABLES
  pc,       \* pc[w]: program counter of worker w
  cur,      \* cur[w]: search executed by w (None if none)
  root,     \* root[w]: root index w is trying to claim (None if none)
  hand,     \* hand[w]: searches w un-suspended (CAS) and not yet enqueued
  hk,       \* hk[w]: toResume.size() captured for the fetch_add(k)
  stalled,  \* stalled[w]: w stopped taking steps forever (A6)
  qIdx,     \* UnrootedStealingQueue::index, capped at NRoots
  active,   \* MultiThreadedTarjan::activeSearches
  sst,      \* sst[s] \in {"none","run","susp","hand","pend","done"}
  waits,    \* waits[s]: search that owns the cell s is suspended on
  pending,  \* Pending queue contents
  budget    \* remaining suspensions (A5)

vars == <<pc, cur, root, hand, hk, stalled, qIdx, active, sst, waits, pending, budget>>

PCs == {"pend",   \* pending.get()
        "empty",  \* cellQueue.isEmpty()
        "inc",    \* activeSearches.fetch_add(1)            (root claim)
        "next",   \* one iteration of cellQueue.next(): index++ ; isNew?
        "claim",  \* getReference + claimOrFail CAS
        "dec",    \* activeSearches.fetch_sub(1)            (failed claim)
        "load",   \* activeSearches.load() == 0 ?
        "sleep",  \* sleep_for(1ms)
        "exec",   \* inside execute(search)
        "rs_inc", \* activeSearches.fetch_add(k)            (resumeAllBlockedOn)
        "rs_enq", \* pending.addPending: one enqueue per step
        "stop",   \* searchStopped(): activeSearches.fetch_sub(1)
        "done"}   \* getSearch returned nullptr: worker thread returned

TypeOK ==
  /\ pc \in [Workers -> PCs]
  /\ cur \in [Workers -> Searches \cup {None}]
  /\ root \in [Workers -> Searches \cup {None}]
  /\ hand \in [Workers -> SUBSET Searches]
  /\ hk \in [Workers -> 0..NRoots]
  /\ stalled \in [Workers -> BOOLEAN]
  /\ qIdx \in 0..NRoots
  /\ active \in Int
  /\ sst \in [Searches -> {"none","run","susp","hand","pend","done"}]
  /\ waits \in [Searches -> Searches \cup {None}]
  /\ pending \subseteq Searches
  /\ budget \in 0..MaxSusp

(***************************************************************************)
(* Wait-for chain helpers (used by the atomic cycle abstraction A3).       *)
(* Chain(t, n) = the searches visited following waits[] from t through     *)
(* suspended searches, at most n hops.                                     *)
(***************************************************************************)
RECURSIVE Chain(_, _)
Chain(t, n) ==
  IF n = 0 \/ t = None \/ sst[t] # "susp" THEN {}
  ELSE {t} \cup Chain(waits[t], n - 1)

\* Following waits[] from t ends at s (s running): s closes a cycle.
RECURSIVE Reaches(_, _, _)
Reaches(t, s, n) ==
  IF t = s THEN TRUE
  ELSE IF n = 0 \/ t = None \/ sst[t] # "susp" THEN FALSE
  ELSE Reaches(waits[t], s, n - 1)

\* Members of the cycle closed by s suspending on t (excluding s).
RECURSIVE CycleMembers(_, _, _)
CycleMembers(t, s, n) ==
  IF t = s \/ n = 0 THEN {} ELSE {t} \cup CycleMembers(waits[t], s, n - 1)

Waiters(s) == {t \in Searches : sst[t] = "susp" /\ waits[t] = s}

Init ==
  /\ pc = [w \in Workers |-> "pend"]
  /\ cur = [w \in Workers |-> None]
  /\ root = [w \in Workers |-> None]
  /\ hand = [w \in Workers |-> {}]
  /\ hk = [w \in Workers |-> 0]
  /\ stalled = [w \in Workers |-> FALSE]
  /\ qIdx = 0
  /\ active = 0
  /\ sst = [s \in Searches |-> "none"]
  /\ waits = [s \in Searches |-> None]
  /\ pending = {}
  /\ budget = MaxSusp

-----------------------------------------------------------------------------
(* getSearch() -- multiThreadedTarjan.cpp                                  *)

\* if(Search* search = pending.get()) return search;
GetPendingOk(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "pend"
  /\ \E s \in pending :
       /\ pending' = pending \ {s}
       /\ sst' = [sst EXCEPT ![s] = "run"]
       /\ cur' = [cur EXCEPT ![w] = s]
       /\ pc' = [pc EXCEPT ![w] = "exec"]
  /\ UNCHANGED <<root, hand, hk, stalled, qIdx, active, waits, budget>>

\* pending.get() returned nullptr (empty, or a spurious miss if SpuriousEmpty)
GetPendingNone(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "pend"
  /\ pending = {} \/ SpuriousEmpty
  /\ pc' = [pc EXCEPT ![w] = "empty"]
  /\ UNCHANGED <<cur, root, hand, hk, stalled, qIdx, active, sst, waits, pending, budget>>

GetPending(w) == GetPendingOk(w) \/ GetPendingNone(w)

\* if(!cellQueue.isEmpty()) ...   (index.load() >= ttlCells)
IsEmpty(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "empty"
  /\ pc' = [pc EXCEPT ![w] = IF qIdx < NRoots
                             THEN (IF IncBeforeTake THEN "inc" ELSE "next")
                             ELSE "load"]
  /\ UNCHANGED <<cur, root, hand, hk, stalled, qIdx, active, sst, waits, pending, budget>>

\* activeSearches.fetch_add(1)
Inc(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "inc"
  /\ active' = active + 1
  /\ pc' = [pc EXCEPT ![w] = IF IncBeforeTake THEN "next" ELSE "claim"]
  /\ UNCHANGED <<cur, root, hand, hk, stalled, qIdx, sst, waits, pending, budget>>

\* One iteration of UnrootedStealingQueue::next(): next = index++; if the
\* slot is beyond ttlCells return null; otherwise the vertex's cell is NEW
\* (return it) or not (loop again).  Outcome of isNew is nondeterministic (A2).
NextRoot(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "next"
  /\ IF qIdx >= NRoots
     THEN \* returned nullWeakReference
          /\ pc' = [pc EXCEPT ![w] = IF IncBeforeTake THEN "dec" ELSE "pend"]
          /\ UNCHANGED <<qIdx, root>>
     ELSE /\ qIdx' = qIdx + 1
          /\ \/ /\ root' = [root EXCEPT ![w] = qIdx + 1]       \* isNew
                /\ pc' = [pc EXCEPT ![w] = IF IncBeforeTake THEN "claim" ELSE "inc"]
             \/ /\ UNCHANGED <<root, pc>>                       \* not new: loop
  /\ UNCHANGED <<cur, hand, hk, stalled, active, sst, waits, pending, budget>>

\* rootReference && rootReference->claimOrFail(worker->spareSearch)
Claim(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "claim"
  /\ LET r == root[w] IN
       \/ /\ sst' = [sst EXCEPT ![r] = "run"]                   \* claim won
          /\ cur' = [cur EXCEPT ![w] = r]
          /\ pc' = [pc EXCEPT ![w] = "exec"]
       \/ /\ pc' = [pc EXCEPT ![w] = "dec"]                     \* claim lost
          /\ UNCHANGED <<sst, cur>>
  /\ root' = [root EXCEPT ![w] = None]
  /\ UNCHANGED <<hand, hk, stalled, qIdx, active, waits, pending, budget>>

\* activeSearches.fetch_sub(1); continue;
Dec(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "dec"
  /\ active' = active - 1
  /\ pc' = [pc EXCEPT ![w] = "pend"]
  /\ UNCHANGED <<cur, root, hand, hk, stalled, qIdx, sst, waits, pending, budget>>

\* if(activeSearches.load() == 0) return nullptr;
LoadActive(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "load"
  /\ pc' = [pc EXCEPT ![w] = IF active = 0 THEN "done" ELSE "sleep"]
  /\ UNCHANGED <<cur, root, hand, hk, stalled, qIdx, active, sst, waits, pending, budget>>

\* std::this_thread::sleep_for(1ms)
Sleep(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "sleep"
  /\ pc' = [pc EXCEPT ![w] = "pend"]
  /\ UNCHANGED <<cur, root, hand, hk, stalled, qIdx, active, sst, waits, pending, budget>>

\* The OS never reschedules w again while it is inside sleep_for (A6).
Stall(w) ==
  /\ AllowStall
  /\ pc[w] = "sleep"
  /\ ~stalled[w]
  /\ Cardinality({v \in Workers : stalled[v]}) < MaxStalls
  /\ stalled' = [stalled EXCEPT ![w] = TRUE]
  /\ UNCHANGED <<pc, cur, root, hand, hk, qIdx, active, sst, waits, pending, budget>>

-----------------------------------------------------------------------------
(* execute(search) -- worker.cpp, abstracted (A3)                          *)

\* The control stack empties: every cell of s is complete.  Every search
\* suspended on one of s's cells has been resumed by s's worker before
\* (resumeAllBlockedOn runs in buildSCC before the search can end).
Finish(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "exec"
  /\ LET s == cur[w] IN
       /\ Waiters(s) = {}
       /\ sst' = [sst EXCEPT ![s] = "done"]
  /\ cur' = [cur EXCEPT ![w] = None]
  /\ pc' = [pc EXCEPT ![w] = "stop"]
  /\ UNCHANGED <<root, hand, hk, stalled, qIdx, active, waits, pending, budget>>

\* claim() returned OCCUPIED for a cell owned by live search t; suspend().
\* If following t's wait-for chain reaches s, s closes a blocking cycle and
\* (atomically, A3) the cycle is resolved by transferring cells to s: s
\* stays running (RESUME), every other member either becomes blocked on a
\* cell s now owns or, with an emptied stack, is done.  Searches waiting on
\* a member that becomes done were waiting on a transferred cell and now
\* wait on s.  Otherwise s is suspended on t and execute() returns.
Suspend(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "exec"
  /\ budget > 0
  /\ LET s == cur[w] IN
     \E t \in Searches \ {s} :
       /\ sst[t] \in {"run", "susp", "hand", "pend"}
       /\ budget' = budget - 1
       /\ IF Reaches(t, s, NRoots)
          THEN LET M == CycleMembers(t, s, NRoots) IN
               \E D \in SUBSET M :
                 /\ sst' = [x \in Searches |-> IF x \in D THEN "done" ELSE sst[x]]
                 /\ waits' = [x \in Searches |->
                                IF x \in D THEN None
                                ELSE IF x \in M THEN s
                                ELSE IF sst[x] = "susp" /\ waits[x] \in D THEN s
                                ELSE waits[x]]
                 /\ UNCHANGED <<pc, cur>>
          ELSE /\ sst' = [sst EXCEPT ![s] = "susp"]
               /\ waits' = [waits EXCEPT ![s] = t]
               /\ cur' = [cur EXCEPT ![w] = None]
               /\ pc' = [pc EXCEPT ![w] = "stop"]
  /\ UNCHANGED <<root, hand, hk, stalled, qIdx, active, pending>>

\* buildSCC/buildSingletonSCC -> resumeAllBlockedOn(cell): bulkUnsuspend CASes
\* the cellBlockedOn of every search on the cell's blocked list (the CASes are
\* collapsed into this step) and collects toResume.
ResumeBlocked(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "exec"
  /\ \E X \in SUBSET Waiters(cur[w]) :
       /\ X # {}
       /\ sst' = [x \in Searches |-> IF x \in X THEN "hand" ELSE sst[x]]
       /\ waits' = [x \in Searches |-> IF x \in X THEN None ELSE waits[x]]
       /\ hand' = [hand EXCEPT ![w] = X]
       /\ hk' = [hk EXCEPT ![w] = Cardinality(X)]
       /\ pc' = [pc EXCEPT ![w] = IF IncBeforeEnqueue THEN "rs_inc" ELSE "rs_enq"]
  /\ UNCHANGED <<cur, root, stalled, qIdx, active, pending, budget>>

\* activeSearches.fetch_add(toResume.size())
RsInc(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "rs_inc"
  /\ active' = active + hk[w]
  /\ pc' = [pc EXCEPT ![w] = IF IncBeforeEnqueue THEN "rs_enq" ELSE "exec"]
  /\ UNCHANGED <<cur, root, hand, hk, stalled, qIdx, sst, waits, pending, budget>>

\* pending.addPending(&toResume): one enqueue per step (A4)
RsEnq(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "rs_enq"
  /\ \E x \in hand[w] :
       /\ pending' = pending \cup {x}
       /\ sst' = [sst EXCEPT ![x] = "pend"]
       /\ hand' = [hand EXCEPT ![w] = hand[w] \ {x}]
       /\ pc' = [pc EXCEPT ![w] =
                   IF hand[w] = {x}
                   THEN (IF IncBeforeEnqueue THEN "exec" ELSE "rs_inc")
                   ELSE "rs_enq"]
  /\ UNCHANGED <<cur, root, hk, stalled, qIdx, active, waits, budget>>

\* scheduler.searchStopped(): activeSearches.fetch_sub(1)
Stop(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "stop"
  /\ active' = active - 1
  /\ pc' = [pc EXCEPT ![w] = "pend"]
  /\ UNCHANGED <<cur, root, hand, hk, stalled, qIdx, sst, waits, pending, budget>>

-----------------------------------------------------------------------------
Step(w) ==
  \/ GetPending(w) \/ IsEmpty(w) \/ Inc(w) \/ NextRoot(w) \/ Claim(w)
  \/ Dec(w) \/ LoadActive(w) \/ Sleep(w)
  \/ Finish(w) \/ Suspend(w) \/ ResumeBlocked(w) \/ RsInc(w) \/ RsEnq(w)
  \/ Stop(w)

\* Next lists the actions individually so that TLC traces name them.
Next == \E w \in Workers :
          \/ GetPending(w) \/ IsEmpty(w) \/ Inc(w) \/ NextRoot(w) \/ Claim(w)
          \/ Dec(w) \/ LoadActive(w) \/ Sleep(w)
          \/ Finish(w) \/ Suspend(w) \/ ResumeBlocked(w) \/ RsInc(w) \/ RsEnq(w)
          \/ Stop(w) \/ Stall(w)

\* Weak fairness for every worker.  With SpuriousEmpty, a worker could miss a
\* non-empty Pending forever; the liveness result then additionally assumes
\* that a repeatedly attempted dequeue of a non-empty queue eventually
\* succeeds (strong fairness on GetPendingOk).
Fairness == \A w \in Workers : WF_vars(Step(w)) /\ SF_vars(GetPendingOk(w))

Spec == Init /\ [][Next]_vars /\ Fairness

-----------------------------------------------------------------------------
(* Properties                                                              *)

Claiming == {w \in Workers : pc[w] \in {"next", "claim", "dec"}}

\* Auxiliary invariant explaining the protocol (real code ordering only):
\* activeSearches = claiming workers + workers executing or about to call
\* searchStopped + searches already counted but not yet enqueued + Pending.
CountInv ==
  active = Cardinality(Claiming)
           + Cardinality({w \in Workers : pc[w] \in {"exec", "rs_inc", "rs_enq", "stop"}})
           + Cardinality(UNION {hand[w] : w \in {v \in Workers : pc[v] = "rs_enq"}})
           + Cardinality(pending)

\* Safety: once any worker has exited, the root queue is exhausted, no worker
\* holds a root it might still claim, and no search is running, pending, or
\* in the middle of being resumed.
NoEarlyExit ==
  (\E w \in Workers : pc[w] = "done") =>
     /\ qIdx >= NRoots
     /\ \A w \in Workers : pc[w] # "claim"
     /\ \A s \in Searches : sst[s] \notin {"run", "pend", "hand"}

\* Safety: once any worker has exited every started search is done
\* (in particular none is left suspended).
DoneOnExit ==
  (\E w \in Workers : pc[w] = "done") => \A s \in Searches : sst[s] \in {"none", "done"}

\* Safety: when all workers have exited, every started search is done.
AllExitedAllDone ==
  (\A w \in Workers : pc[w] = "done") => \A s \in Searches : sst[s] \in {"none", "done"}

\* Sanity of the abstraction: suspended searches never form a cycle.
NoSuspendedCycle ==
  \A s \in Searches : sst[s] = "susp" => ~Reaches(waits[s], s, NRoots)

\* Liveness: every worker that is not stalled eventually returns.
Termination == <>(\A w \in Workers : pc[w] = "done" \/ stalled[w])

\* Symmetry set for safety-only configurations (unsound for liveness).
WorkerSymmetry == Permutations(Workers)

=============================================================================
