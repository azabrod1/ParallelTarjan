--------------------------- MODULE TerminationFlags ---------------------------
(***************************************************************************)
(* OLD termination protocol at git 8a9f1a9: one bit per worker in the      *)
(* 64-bit word `flags`.                                                    *)
(*                                                                         *)
(* Code mapped (paths relative to "Tarjan Code/" at 8a9f1a9):              *)
(*   worker.cpp   Worker::operator(): loop getSearch -> execute            *)
(*                (no searchStopped; there is no counter)                  *)
(*   multiThreadedTarjan.cpp  MultiThreadedTarjan::getSearch:              *)
(*        flags.store(0); updateFlagsASAP = true; done = false;            *)
(*        while(!done){                                                    *)
(*          search = pending.get(); if(search) return search;              *)
(*          root = cellQueue.next(worker);                                 *)
(*          if(root.get()) { ...claimOrFail... return search; }            *)
(*          if(updateFlagsASAP) flags |= mask;                             *)
(*          updateFlagsASAP = (!updateFlagsASAP &&                         *)
(*                             ((flags.load() & mask) == 0));              *)
(*          done = (flags.load() == ALL_FLAGS_SET);                        *)
(*          if(!done) sleep_for(1ms);                                      *)
(*        }                                                                *)
(*        return nullptr;                                                  *)
(*   multiThreadedTarjan.hpp  resumeAllBlockedOn: bulkUnsuspend then       *)
(*        addPending (no counter).                                         *)
(*                                                                         *)
(* The abstraction of searches, roots, Pending and execute() is identical  *)
(* to Termination.tla (A1-A6 there).  Only the scheduler differs.          *)
(* Every atomic access (flags.store, flags |= mask (fetch_or),             *)
(* flags.load, pending.get/enqueue, index++, claim CAS) is its own step.   *)
(* `if(updateFlagsASAP) flags |= mask;` followed by the short-circuit      *)
(* update of updateFlagsASAP is one step when updateFlagsASAP is true (the *)
(* update then touches no shared state) and one load when it is false.    *)
(***************************************************************************)
EXTENDS Integers, FiniteSets, TLC

CONSTANTS Workers, NRoots, MaxSusp, AllowStall,
          MaxStalls,     \* at most this many workers stall
          RealisticRoots \* TRUE: a root can be skipped (not NEW) or lose its
                         \* claim only if some OTHER search has started, since
                         \* only a running search's DFS can claim a root vertex

ASSUME NRoots \in Nat /\ MaxSusp \in Nat /\ AllowStall \in BOOLEAN /\ MaxStalls \in Nat
ASSUME RealisticRoots \in BOOLEAN


Searches == 1..NRoots
None     == 0

VARIABLES
  pc, cur, root, hand, stalled, qIdx, sst, waits, pending, budget,
  flags,   \* the shared word, as the set of workers whose bit is set
  asap     \* asap[w]: worker-local updateFlagsASAP

vars == <<pc, cur, root, hand, stalled, qIdx, sst, waits, pending, budget, flags, asap>>

PCs == {"clear",  \* flags.store(0) at entry of getSearch
        "pend",   \* pending.get()
        "next",   \* one iteration of cellQueue.next(): index++ ; isNew?
        "claim",  \* getReference + claimOrFail
        "setbit", \* if(asap) flags |= mask  /  else flags.load() & mask
        "chk",    \* done = (flags.load() == ALL_FLAGS_SET)
        "sleep",  \* sleep_for(1ms)
        "exec",   \* execute(search)
        "rs_enq", \* addPending, one enqueue per step
        "done"}   \* getSearch returned nullptr

TypeOK ==
  /\ pc \in [Workers -> PCs]
  /\ cur \in [Workers -> Searches \cup {None}]
  /\ root \in [Workers -> Searches \cup {None}]
  /\ hand \in [Workers -> SUBSET Searches]
  /\ stalled \in [Workers -> BOOLEAN]
  /\ qIdx \in 0..NRoots
  /\ sst \in [Searches -> {"none","run","susp","hand","pend","done"}]
  /\ waits \in [Searches -> Searches \cup {None}]
  /\ pending \subseteq Searches
  /\ budget \in 0..MaxSusp
  /\ flags \subseteq Workers
  /\ asap \in [Workers -> BOOLEAN]

RECURSIVE Reaches(_, _, _)
Reaches(t, s, n) ==
  IF t = s THEN TRUE
  ELSE IF n = 0 \/ t = None \/ sst[t] # "susp" THEN FALSE
  ELSE Reaches(waits[t], s, n - 1)

RECURSIVE CycleMembers(_, _, _)
CycleMembers(t, s, n) ==
  IF t = s \/ n = 0 THEN {} ELSE {t} \cup CycleMembers(waits[t], s, n - 1)

Waiters(s) == {t \in Searches : sst[t] = "susp" /\ waits[t] = s}

\* Some search other than root r has started (so r's vertex may be taken).
OtherStarted(r) == \E s \in Searches \ {r} : sst[s] # "none"

\* Worker::operator() calls getSearch first, which starts with flags.store(0).
Init ==
  /\ pc = [w \in Workers |-> "clear"]
  /\ cur = [w \in Workers |-> None]
  /\ root = [w \in Workers |-> None]
  /\ hand = [w \in Workers |-> {}]
  /\ stalled = [w \in Workers |-> FALSE]
  /\ qIdx = 0
  /\ sst = [s \in Searches |-> "none"]
  /\ waits = [s \in Searches |-> None]
  /\ pending = {}
  /\ budget = MaxSusp
  /\ flags = {}
  /\ asap = [w \in Workers |-> TRUE]

-----------------------------------------------------------------------------
(* getSearch() at 8a9f1a9                                                  *)

\* flags.store(0); updateFlagsASAP = true;
Clear(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "clear"
  /\ flags' = {}
  /\ asap' = [asap EXCEPT ![w] = TRUE]
  /\ pc' = [pc EXCEPT ![w] = "pend"]
  /\ UNCHANGED <<cur, root, hand, stalled, qIdx, sst, waits, pending, budget>>

\* search = pending.get(); if(search) return search;
GetPending(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "pend"
  /\ IF pending # {}
     THEN \E s \in pending :
            /\ pending' = pending \ {s}
            /\ sst' = [sst EXCEPT ![s] = "run"]
            /\ cur' = [cur EXCEPT ![w] = s]
            /\ pc' = [pc EXCEPT ![w] = "exec"]
     ELSE /\ pc' = [pc EXCEPT ![w] = "next"]
          /\ UNCHANGED <<pending, sst, cur>>
  /\ UNCHANGED <<root, hand, stalled, qIdx, waits, budget, flags, asap>>

\* One iteration of cellQueue.next(): index++; null if exhausted; isNew?
NextRoot(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "next"
  /\ IF qIdx >= NRoots
     THEN /\ pc' = [pc EXCEPT ![w] = "setbit"]     \* root.get() == nullptr
          /\ UNCHANGED <<qIdx, root>>
     ELSE /\ qIdx' = qIdx + 1
          /\ \/ /\ root' = [root EXCEPT ![w] = qIdx + 1]
                /\ pc' = [pc EXCEPT ![w] = "claim"]
             \/ /\ RealisticRoots => OtherStarted(qIdx + 1)   \* not NEW: loop
                /\ UNCHANGED <<root, pc>>
  /\ UNCHANGED <<cur, hand, stalled, sst, waits, pending, budget, flags, asap>>

\* claimOrFail; on failure fall through to the flag update
Claim(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "claim"
  /\ LET r == root[w] IN
       \/ /\ sst' = [sst EXCEPT ![r] = "run"]
          /\ cur' = [cur EXCEPT ![w] = r]
          /\ pc' = [pc EXCEPT ![w] = "exec"]
       \/ /\ RealisticRoots => OtherStarted(r)           \* claim lost
          /\ pc' = [pc EXCEPT ![w] = "setbit"]
          /\ UNCHANGED <<sst, cur>>
  /\ root' = [root EXCEPT ![w] = None]
  /\ UNCHANGED <<hand, stalled, qIdx, waits, pending, budget, flags, asap>>

\* if(updateFlagsASAP) flags |= mask;
\* updateFlagsASAP = (!updateFlagsASAP && ((flags.load() & mask) == 0));
SetBit(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "setbit"
  /\ IF asap[w]
     THEN /\ flags' = flags \cup {w}                 \* fetch_or
          /\ asap' = [asap EXCEPT ![w] = FALSE]      \* short-circuit: no load
     ELSE /\ asap' = [asap EXCEPT ![w] = (w \notin flags)]   \* flags.load()
          /\ UNCHANGED flags
  /\ pc' = [pc EXCEPT ![w] = "chk"]
  /\ UNCHANGED <<cur, root, hand, stalled, qIdx, sst, waits, pending, budget>>

\* done = (flags.load() == ALL_FLAGS_SET); if(!done) sleep
Check(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "chk"
  /\ pc' = [pc EXCEPT ![w] = IF flags = Workers THEN "done" ELSE "sleep"]
  /\ UNCHANGED <<cur, root, hand, stalled, qIdx, sst, waits, pending, budget, flags, asap>>

Sleep(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "sleep"
  /\ pc' = [pc EXCEPT ![w] = "pend"]
  /\ UNCHANGED <<cur, root, hand, stalled, qIdx, sst, waits, pending, budget, flags, asap>>

Stall(w) ==
  /\ AllowStall
  /\ pc[w] = "sleep"
  /\ ~stalled[w]
  /\ Cardinality({v \in Workers : stalled[v]}) < MaxStalls
  /\ stalled' = [stalled EXCEPT ![w] = TRUE]
  /\ UNCHANGED <<pc, cur, root, hand, qIdx, sst, waits, pending, budget, flags, asap>>

-----------------------------------------------------------------------------
(* execute(search), abstracted exactly as in Termination.tla              *)

Finish(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "exec"
  /\ Waiters(cur[w]) = {}
  /\ sst' = [sst EXCEPT ![cur[w]] = "done"]
  /\ cur' = [cur EXCEPT ![w] = None]
  /\ pc' = [pc EXCEPT ![w] = "clear"]
  /\ UNCHANGED <<root, hand, stalled, qIdx, waits, pending, budget, flags, asap>>

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
               /\ pc' = [pc EXCEPT ![w] = "clear"]
  /\ UNCHANGED <<root, hand, stalled, qIdx, pending, flags, asap>>

ResumeBlocked(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "exec"
  /\ \E X \in SUBSET Waiters(cur[w]) :
       /\ X # {}
       /\ sst' = [x \in Searches |-> IF x \in X THEN "hand" ELSE sst[x]]
       /\ waits' = [x \in Searches |-> IF x \in X THEN None ELSE waits[x]]
       /\ hand' = [hand EXCEPT ![w] = X]
       /\ pc' = [pc EXCEPT ![w] = "rs_enq"]
  /\ UNCHANGED <<cur, root, stalled, qIdx, pending, budget, flags, asap>>

RsEnq(w) ==
  /\ ~stalled[w]
  /\ pc[w] = "rs_enq"
  /\ \E x \in hand[w] :
       /\ pending' = pending \cup {x}
       /\ sst' = [sst EXCEPT ![x] = "pend"]
       /\ hand' = [hand EXCEPT ![w] = hand[w] \ {x}]
       /\ pc' = [pc EXCEPT ![w] = IF hand[w] = {x} THEN "exec" ELSE "rs_enq"]
  /\ UNCHANGED <<cur, root, stalled, qIdx, waits, budget, flags, asap>>

-----------------------------------------------------------------------------
Step(w) ==
  \/ Clear(w) \/ GetPending(w) \/ NextRoot(w) \/ Claim(w) \/ SetBit(w)
  \/ Check(w) \/ Sleep(w) \/ Finish(w) \/ Suspend(w) \/ ResumeBlocked(w)
  \/ RsEnq(w)

Next == \E w \in Workers :
          \/ Clear(w) \/ GetPending(w) \/ NextRoot(w) \/ Claim(w) \/ SetBit(w)
          \/ Check(w) \/ Sleep(w) \/ Finish(w) \/ Suspend(w) \/ ResumeBlocked(w)
          \/ RsEnq(w) \/ Stall(w)

Fairness == \A w \in Workers : WF_vars(Step(w))

Spec == Init /\ [][Next]_vars /\ Fairness

-----------------------------------------------------------------------------
(* The same properties as Termination.tla                                  *)

NoEarlyExit ==
  (\E w \in Workers : pc[w] = "done") =>
     /\ qIdx >= NRoots
     /\ \A w \in Workers : pc[w] # "claim"
     /\ \A s \in Searches : sst[s] \notin {"run", "pend", "hand"}

DoneOnExit ==
  (\E w \in Workers : pc[w] = "done") => \A s \in Searches : sst[s] \in {"none", "done"}

AllExitedAllDone ==
  (\A w \in Workers : pc[w] = "done") => \A s \in Searches : sst[s] \in {"none", "done"}

\* The audit's "stale bit": w is executing a search while its bit is set.
NoStaleBit == \A w \in Workers : pc[w] \in {"exec", "rs_enq"} => w \notin flags

Termination == <>(\A w \in Workers : pc[w] = "done" \/ stalled[w])

\* Symmetry set for safety-only configurations (unsound for liveness).
WorkerSymmetry == Permutations(Workers)

=============================================================================
