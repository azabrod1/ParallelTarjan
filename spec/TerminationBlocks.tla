--------------------------- MODULE TerminationBlocks ---------------------------
(***************************************************************************)
(* Termination protocol at git 2a280d9 (activeSearches counter with        *)
(* per-worker blocks of root vertices).                                    *)
(*                                                                         *)
(* Code mapped (paths at 2a280d9):                                         *)
(*   src/parallel/worker.cpp   Worker::operator(): while(getSearch) {      *)
(*                             execute; searchStopped }                    *)
(*   src/parallel/parallel_tarjan.cpp  ParallelTarjan::getSearch:          *)
(*     loop {                                                              *)
(*      (1) if(search = pending.get()) return search;                      *)
(*      (2) if(worker.holdsRoots()) {                                      *)
(*            root = worker.nextLocalRoot();                               *)
(*            if(root && root->claimOrFail(spare)) {                       *)
(*              if(worker.holdsRoots()) activeSearches.fetch_add(1);       *)
(*              return search; }                                           *)
(*            if(!worker.holdsRoots()) activeSearches.fetch_sub(1);        *)
(*            continue; }                                                  *)
(*      (3) if(!cellQueue.isEmpty()) { activeSearches.fetch_add(1);        *)
(*            if(!cellQueue.nextChunk(worker, range))                      *)
(*              activeSearches.fetch_sub(1);                               *)
(*            continue; }                                                  *)
(*      (4) if(activeSearches.load() == 0) return nullptr;                 *)
(*      (5) sleep_for(1ms); }                                              *)
(*   src/parallel/worker.hpp   nextLocalRoot(): while(next < end) {        *)
(*                               c = cell(next++); if(c->isNew()) return c;}*)
(*                             holdsRoots(): next < end                    *)
(*   src/parallel/root_queue.hpp  VertexOrderRoots::nextChunk:             *)
(*                             first = index.fetch_add(chunk);             *)
(*                             isEmpty(): index.load() >= ttlCells         *)
(*   src/parallel/parallel_tarjan.hpp  resumeAllBlockedOn: +k then         *)
(*                             addPending; searchStopped: -1               *)
(*                                                                         *)
(* Each atomic access to shared state is one step.  Steps that read only   *)
(* worker-local state (holdsRoots(), the `root &&` test) are folded into   *)
(* the neighbouring step.  claimOrFail() is `status == NEW && CAS`; the    *)
(* load only lets the CAS fail early, so the pair is one step (the CAS     *)
(* succeeds iff the vertex is still NEW at that instant).                  *)
(*                                                                         *)
(* ABSTRACTIONS (README.md has the full list):                             *)
(*  B1 Vertices 1..NV are the root candidates (every vertex is offered     *)
(*     once).  vstat[v] is NEW until some search claims v, either as a     *)
(*     root (claimOrFail) or during its DFS (abstract action DfsClaim).    *)
(*     A search is identified by the vertex it was rooted at.              *)
(*  B2 execute() is abstracted exactly as in Termination.tla (A3): finish, *)
(*     suspend on a live search, resolve a cycle atomically, resume        *)
(*     waiters (+k then one enqueue per step), plus DFS claims.  The       *)
(*     suspend handshake and cycle detection are checked separately in     *)
(*     Suspension.tla and CycleDetection.tla; this model ASSUMES them.     *)
(*  B3 Pending is a set; SpuriousEmpty models moodycamel try_dequeue,      *)
(*     which may return false on a non-empty queue.                        *)
(*  B4 MaxSusp bounds suspensions; the `aborted` exception path is not     *)
(*     modelled.                                                           *)
(*  B5 A worker may stall forever only inside sleep_for (holding nothing). *)
(***************************************************************************)
EXTENDS Integers, FiniteSets, TLC

CONSTANTS Workers,       \* worker ids
          NV,            \* number of vertices (= ttlCells)
          Chunk,         \* VertexOrderRoots::chunkSize
          MaxSusp,       \* bound on suspensions
          AllowStall,    \* may an idle worker stall forever in sleep_for?
          MaxStalls,     \* at most this many stalled workers
          SpuriousEmpty, \* pending.get() may miss a non-empty queue
          CountBlockFirst \* TRUE = real code: fetch_add(1) BEFORE nextChunk.
                          \* FALSE is a mutation (count after taking the block).

ASSUME NV \in Nat /\ Chunk \in Nat \ {0} /\ MaxSusp \in Nat /\ MaxStalls \in Nat
ASSUME AllowStall \in BOOLEAN /\ SpuriousEmpty \in BOOLEAN /\ CountBlockFirst \in BOOLEAN

Vertices == 1..NV
Searches == Vertices
None     == 0
Min(a, b) == IF a < b THEN a ELSE b

VARIABLES
  pc, cur,
  rnext, rend,  \* worker's RootRange, 0-based [rnext, rend)
  root,         \* vertex returned by nextLocalRoot, being claimed
  bc,           \* ghost: 1 iff w's block is currently counted in activeSearches
  hand, hk, stalled,
  index,        \* VertexOrderRoots::index, capped at NV
  active,       \* activeSearches
  vstat,        \* vstat[v] \in {"new","claimed"}
  examined,     \* ghost: vertices nextLocalRoot has looked at
  sst, waits, pending, budget

vars == <<pc, cur, rnext, rend, root, bc, hand, hk, stalled, index, active,
          vstat, examined, sst, waits, pending, budget>>

PCs == {"pend",  \* pending.get()
        "scan",  \* one iteration of nextLocalRoot: cell(next++)->isNew()
        "claim", \* claimOrFail
        "cnt",   \* activeSearches.fetch_add(1) for the new search (block not used up)
        "rel",   \* activeSearches.fetch_sub(1) (block used up, no search started)
        "empty", \* cellQueue.isEmpty()
        "inc",   \* activeSearches.fetch_add(1) before nextChunk
        "chunk", \* nextChunk: index.fetch_add(Chunk)
        "dec",   \* activeSearches.fetch_sub(1) (nextChunk failed)
        "load",  \* activeSearches.load() == 0 ?
        "sleep", \* sleep_for(1ms)
        "exec", "rs_inc", "rs_enq",
        "stop",  \* searchStopped()
        "done"}

Holds(w) == rnext[w] < rend[w]

TypeOK ==
  /\ pc \in [Workers -> PCs]
  /\ cur \in [Workers -> Searches \cup {None}]
  /\ rnext \in [Workers -> 0..NV] /\ rend \in [Workers -> 0..NV]
  /\ root \in [Workers -> Vertices \cup {None}]
  /\ bc \in [Workers -> {0, 1}]
  /\ hand \in [Workers -> SUBSET Searches]
  /\ hk \in [Workers -> 0..NV]
  /\ stalled \in [Workers -> BOOLEAN]
  /\ index \in 0..NV
  /\ active \in Int
  /\ vstat \in [Vertices -> {"new", "claimed"}]
  /\ examined \subseteq Vertices
  /\ sst \in [Searches -> {"none","run","susp","hand","pend","done"}]
  /\ waits \in [Searches -> Searches \cup {None}]
  /\ pending \subseteq Searches
  /\ budget \in 0..MaxSusp

RECURSIVE Reaches(_, _, _)
Reaches(t, s, n) ==
  IF t = s THEN TRUE
  ELSE IF n = 0 \/ t = None \/ sst[t] # "susp" THEN FALSE
  ELSE Reaches(waits[t], s, n - 1)

RECURSIVE CycleMembers(_, _, _)
CycleMembers(t, s, n) ==
  IF t = s \/ n = 0 THEN {} ELSE {t} \cup CycleMembers(waits[t], s, n - 1)

Waiters(s) == {t \in Searches : sst[t] = "susp" /\ waits[t] = s}

Init ==
  /\ pc = [w \in Workers |-> "pend"]
  /\ cur = [w \in Workers |-> None]
  /\ rnext = [w \in Workers |-> 0]
  /\ rend = [w \in Workers |-> 0]
  /\ root = [w \in Workers |-> None]
  /\ bc = [w \in Workers |-> 0]
  /\ hand = [w \in Workers |-> {}]
  /\ hk = [w \in Workers |-> 0]
  /\ stalled = [w \in Workers |-> FALSE]
  /\ index = 0
  /\ active = 0
  /\ vstat = [v \in Vertices |-> "new"]
  /\ examined = {}
  /\ sst = [s \in Searches |-> "none"]
  /\ waits = [s \in Searches |-> None]
  /\ pending = {}
  /\ budget = MaxSusp

-----------------------------------------------------------------------------
(* getSearch()                                                              *)

\* (1) pending.get() succeeds
GetPendingOk(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "pend"
  /\ \E s \in pending :
       /\ pending' = pending \ {s}
       /\ sst' = [sst EXCEPT ![s] = "run"]
       /\ cur' = [cur EXCEPT ![w] = s]
       /\ pc' = [pc EXCEPT ![w] = "exec"]
  /\ UNCHANGED <<rnext, rend, root, bc, hand, hk, stalled, index, active, vstat,
                 examined, waits, budget>>

\* (1) pending.get() returns nullptr; then the local holdsRoots() test
GetPendingNone(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "pend"
  /\ pending = {} \/ SpuriousEmpty
  /\ pc' = [pc EXCEPT ![w] = IF Holds(w) THEN "scan" ELSE "empty"]
  /\ UNCHANGED <<cur, rnext, rend, root, bc, hand, hk, stalled, index, active, vstat,
                 examined, sst, waits, pending, budget>>

\* (2) one iteration of nextLocalRoot(): candidate = cell(next++); isNew()?
\*     When the loop ends without a NEW cell, root is nullptr; then
\*     `if(!holdsRoots()) fetch_sub` (holdsRoots is necessarily false here).
Scan(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "scan"
  /\ IF Holds(w)
     THEN LET v == rnext[w] + 1 IN          \* 0-based slot -> vertex id
          /\ rnext' = [rnext EXCEPT ![w] = rnext[w] + 1]
          /\ examined' = examined \cup {v}
          /\ IF vstat[v] = "new"
             THEN /\ root' = [root EXCEPT ![w] = v]
                  /\ pc' = [pc EXCEPT ![w] = "claim"]
             ELSE UNCHANGED <<root, pc>>                   \* keep scanning
     ELSE /\ pc' = [pc EXCEPT ![w] = "rel"]                \* returned nullptr
          /\ UNCHANGED <<rnext, examined, root>>
  /\ UNCHANGED <<cur, rend, bc, hand, hk, stalled, index, active, vstat, sst, waits,
                 pending, budget>>

\* (2) claimOrFail; then the local holdsRoots() test decides whether the new
\*     search needs its own count or inherits the used-up block's count.
Claim(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "claim"
  /\ LET v == root[w] IN
     IF vstat[v] = "new"
     THEN /\ vstat' = [vstat EXCEPT ![v] = "claimed"]
          /\ sst' = [sst EXCEPT ![v] = "run"]
          /\ cur' = [cur EXCEPT ![w] = v]
          /\ IF Holds(w)
             THEN /\ pc' = [pc EXCEPT ![w] = "cnt"]
                  /\ UNCHANGED bc
             ELSE /\ pc' = [pc EXCEPT ![w] = "exec"]       \* count passes to search
                  /\ bc' = [bc EXCEPT ![w] = 0]
     ELSE /\ pc' = [pc EXCEPT ![w] = IF Holds(w) THEN "pend" ELSE "rel"]
          /\ UNCHANGED <<vstat, sst, cur, bc>>
  /\ root' = [root EXCEPT ![w] = None]
  /\ UNCHANGED <<rnext, rend, hand, hk, stalled, index, active, examined, waits,
                 pending, budget>>

\* activeSearches.fetch_add(1): count the new search, the block keeps its count
Cnt(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "cnt"
  /\ active' = active + 1
  /\ pc' = [pc EXCEPT ![w] = "exec"]
  /\ UNCHANGED <<cur, rnext, rend, root, bc, hand, hk, stalled, index, vstat, examined,
                 sst, waits, pending, budget>>

\* activeSearches.fetch_sub(1): the block is used up
Rel(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "rel"
  /\ active' = active - 1
  /\ bc' = [bc EXCEPT ![w] = 0]
  /\ pc' = [pc EXCEPT ![w] = "pend"]
  /\ UNCHANGED <<cur, rnext, rend, root, hand, hk, stalled, index, vstat, examined,
                 sst, waits, pending, budget>>

\* (3) cellQueue.isEmpty()
IsEmpty(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "empty"
  /\ pc' = [pc EXCEPT ![w] = IF index < NV
                             THEN (IF CountBlockFirst THEN "inc" ELSE "chunk")
                             ELSE "load"]
  /\ UNCHANGED <<cur, rnext, rend, root, bc, hand, hk, stalled, index, active, vstat,
                 examined, sst, waits, pending, budget>>

\* (3) activeSearches.fetch_add(1) (count the block before taking it)
Inc(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "inc"
  /\ active' = active + 1
  /\ bc' = [bc EXCEPT ![w] = 1]
  /\ pc' = [pc EXCEPT ![w] = IF CountBlockFirst THEN "chunk" ELSE "pend"]
  /\ UNCHANGED <<cur, rnext, rend, root, hand, hk, stalled, index, vstat, examined,
                 sst, waits, pending, budget>>

\* (3) nextChunk: first = index.fetch_add(Chunk); false if first >= ttlCells
NextChunk(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "chunk"
  /\ index' = Min(index + Chunk, NV)
  /\ IF index >= NV
     THEN /\ pc' = [pc EXCEPT ![w] = IF CountBlockFirst THEN "dec" ELSE "pend"]
          /\ UNCHANGED <<rnext, rend>>
     ELSE /\ rnext' = [rnext EXCEPT ![w] = index]
          /\ rend' = [rend EXCEPT ![w] = Min(index + Chunk, NV)]
          /\ pc' = [pc EXCEPT ![w] = IF CountBlockFirst THEN "pend" ELSE "inc"]
  /\ UNCHANGED <<cur, root, bc, hand, hk, stalled, active, vstat, examined, sst, waits,
                 pending, budget>>

\* (3) activeSearches.fetch_sub(1): another worker took the last block
Dec(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "dec"
  /\ active' = active - 1
  /\ bc' = [bc EXCEPT ![w] = 0]
  /\ pc' = [pc EXCEPT ![w] = "pend"]
  /\ UNCHANGED <<cur, rnext, rend, root, hand, hk, stalled, index, vstat, examined,
                 sst, waits, pending, budget>>

\* (4) activeSearches.load() == 0
LoadActive(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "load"
  /\ pc' = [pc EXCEPT ![w] = IF active = 0 THEN "done" ELSE "sleep"]
  /\ UNCHANGED <<cur, rnext, rend, root, bc, hand, hk, stalled, index, active, vstat,
                 examined, sst, waits, pending, budget>>

\* (5) sleep_for(1ms)
Sleep(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "sleep"
  /\ pc' = [pc EXCEPT ![w] = "pend"]
  /\ UNCHANGED <<cur, rnext, rend, root, bc, hand, hk, stalled, index, active, vstat,
                 examined, sst, waits, pending, budget>>

Stall(w) ==
  /\ AllowStall
  /\ pc[w] = "sleep"
  /\ ~stalled[w]
  /\ Cardinality({v \in Workers : stalled[v]}) < MaxStalls
  /\ stalled' = [stalled EXCEPT ![w] = TRUE]
  /\ UNCHANGED <<pc, cur, rnext, rend, root, bc, hand, hk, index, active, vstat,
                 examined, sst, waits, pending, budget>>

-----------------------------------------------------------------------------
(* execute(search), abstracted (B2)                                        *)

\* The DFS of the running search claims a still-NEW vertex (claim()).
DfsClaim(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "exec"
  /\ \E v \in Vertices :
       /\ vstat[v] = "new"
       /\ vstat' = [vstat EXCEPT ![v] = "claimed"]
  /\ UNCHANGED <<pc, cur, rnext, rend, root, bc, hand, hk, stalled, index, active,
                 examined, sst, waits, pending, budget>>

Finish(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "exec"
  /\ Waiters(cur[w]) = {}
  /\ sst' = [sst EXCEPT ![cur[w]] = "done"]
  /\ cur' = [cur EXCEPT ![w] = None]
  /\ pc' = [pc EXCEPT ![w] = "stop"]
  /\ UNCHANGED <<rnext, rend, root, bc, hand, hk, stalled, index, active, vstat,
                 examined, waits, pending, budget>>

Suspend(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "exec"
  /\ budget > 0
  /\ LET s == cur[w] IN
     \E t \in Searches \ {s} :
       /\ sst[t] \in {"run", "susp", "hand", "pend"}
       /\ budget' = budget - 1
       /\ IF Reaches(t, s, NV)
          THEN LET M == CycleMembers(t, s, NV) IN
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
  /\ UNCHANGED <<rnext, rend, root, bc, hand, hk, stalled, index, active, vstat,
                 examined, pending>>

ResumeBlocked(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "exec"
  /\ \E X \in SUBSET Waiters(cur[w]) :
       /\ X # {}
       /\ sst' = [x \in Searches |-> IF x \in X THEN "hand" ELSE sst[x]]
       /\ waits' = [x \in Searches |-> IF x \in X THEN None ELSE waits[x]]
       /\ hand' = [hand EXCEPT ![w] = X]
       /\ hk' = [hk EXCEPT ![w] = Cardinality(X)]
       /\ pc' = [pc EXCEPT ![w] = "rs_inc"]
  /\ UNCHANGED <<cur, rnext, rend, root, bc, stalled, index, active, vstat, examined,
                 pending, budget>>

RsInc(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "rs_inc"
  /\ active' = active + hk[w]
  /\ pc' = [pc EXCEPT ![w] = "rs_enq"]
  /\ UNCHANGED <<cur, rnext, rend, root, bc, hand, hk, stalled, index, vstat, examined,
                 sst, waits, pending, budget>>

RsEnq(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "rs_enq"
  /\ \E x \in hand[w] :
       /\ pending' = pending \cup {x}
       /\ sst' = [sst EXCEPT ![x] = "pend"]
       /\ hand' = [hand EXCEPT ![w] = hand[w] \ {x}]
       /\ pc' = [pc EXCEPT ![w] = IF hand[w] = {x} THEN "exec" ELSE "rs_enq"]
  /\ UNCHANGED <<cur, rnext, rend, root, bc, hk, stalled, index, active, vstat, examined,
                 waits, budget>>

Stop(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "stop"
  /\ active' = active - 1
  /\ pc' = [pc EXCEPT ![w] = "pend"]
  /\ UNCHANGED <<cur, rnext, rend, root, bc, hand, hk, stalled, index, vstat, examined,
                 sst, waits, pending, budget>>

-----------------------------------------------------------------------------
Step(w) ==
  \/ GetPendingOk(w) \/ GetPendingNone(w) \/ Scan(w) \/ Claim(w) \/ Cnt(w) \/ Rel(w)
  \/ IsEmpty(w) \/ Inc(w) \/ NextChunk(w) \/ Dec(w) \/ LoadActive(w) \/ Sleep(w)
  \/ DfsClaim(w) \/ Finish(w) \/ Suspend(w) \/ ResumeBlocked(w) \/ RsInc(w)
  \/ RsEnq(w) \/ Stop(w)

Next == \E w \in Workers :
          \/ GetPendingOk(w) \/ GetPendingNone(w) \/ Scan(w) \/ Claim(w) \/ Cnt(w)
          \/ Rel(w) \/ IsEmpty(w) \/ Inc(w) \/ NextChunk(w) \/ Dec(w)
          \/ LoadActive(w) \/ Sleep(w) \/ DfsClaim(w) \/ Finish(w) \/ Suspend(w)
          \/ ResumeBlocked(w) \/ RsInc(w) \/ RsEnq(w) \/ Stop(w) \/ Stall(w)

\* DfsClaim is optional work, so it is excluded from the fairness obligation:
\* WF on the remaining steps.  SF on a successful dequeue (see Termination.tla).
Progress(w) ==
  \/ GetPendingOk(w) \/ GetPendingNone(w) \/ Scan(w) \/ Claim(w) \/ Cnt(w) \/ Rel(w)
  \/ IsEmpty(w) \/ Inc(w) \/ NextChunk(w) \/ Dec(w) \/ LoadActive(w) \/ Sleep(w)
  \/ Finish(w) \/ Suspend(w) \/ ResumeBlocked(w) \/ RsInc(w) \/ RsEnq(w) \/ Stop(w)
Fairness == \A w \in Workers : WF_vars(Progress(w)) /\ SF_vars(GetPendingOk(w))

Spec == Init /\ [][Next]_vars /\ Fairness

-----------------------------------------------------------------------------
(* Properties                                                              *)

\* Auxiliary invariant (real code only): the count is the counted blocks plus
\* searches running / about to call searchStopped, plus counted-not-enqueued
\* resumptions, plus Pending.
CountInv ==
  active = Cardinality({w \in Workers : bc[w] = 1})
           + Cardinality({w \in Workers : pc[w] \in {"exec", "rs_inc", "rs_enq", "stop"}})
           + Cardinality(UNION {hand[w] : w \in {v \in Workers : pc[v] = "rs_enq"}})
           + Cardinality(pending)

\* A counted block is held, or its count is about to be released / passed on.
BlockInv ==
  \A w \in Workers : Holds(w) => (bc[w] = 1 \/ ~CountBlockFirst)

\* Safety: no worker exits while a search is running/pending/being resumed, a
\* root is being claimed, a block is held with unexamined vertices, or a
\* vertex has not been handed out.
NoEarlyExit ==
  (\E w \in Workers : pc[w] = "done") =>
     /\ index >= NV
     /\ \A w \in Workers : ~Holds(w) /\ pc[w] \notin {"claim", "cnt"}
     /\ \A s \in Searches : sst[s] \notin {"run", "pend", "hand"}

\* Safety: once any worker exits, every vertex has been examined by a block
\* holder or claimed, every vertex is claimed (so it belongs to some search),
\* and every started search is done.
DoneOnExit ==
  (\E w \in Workers : pc[w] = "done") =>
     /\ \A v \in Vertices : v \in examined \/ vstat[v] = "claimed"
     /\ \A v \in Vertices : vstat[v] = "claimed"
     /\ \A s \in Searches : sst[s] \in {"none", "done"}

AllExitedAllDone ==
  (\A w \in Workers : pc[w] = "done") =>
     /\ \A v \in Vertices : v \in examined \/ vstat[v] = "claimed"
     /\ \A v \in Vertices : vstat[v] = "claimed"
     /\ \A s \in Searches : sst[s] \in {"none", "done"}

NoSuspendedCycle ==
  \A s \in Searches : sst[s] = "susp" => ~Reaches(waits[s], s, NV)

Termination == <>(\A w \in Workers : pc[w] = "done" \/ stalled[w])

\* Symmetry set for safety-only configurations (unsound for liveness).
WorkerSymmetry == Permutations(Workers)

=============================================================================
