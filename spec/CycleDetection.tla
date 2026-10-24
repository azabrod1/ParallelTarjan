---------------------------- MODULE CycleDetection ----------------------------
(***************************************************************************)
(* Blocking-cycle detection, resolver consensus and cell transfer of       *)
(* SuspensionManager::suspend at git 2a280d9                               *)
(* (src/parallel/suspension_manager.cpp, search.cpp transferCells).        *)
(*                                                                         *)
(*  suspend(worker, Sn, conflictCell):                                     *)
(*   "sa"  Sn_Age = Sn->age                                     (load)     *)
(*   "sb"  addToBlockedList(Sn); Sn->suspendOn(conflictCell)    (store)    *)
(*   "sc"  if(conflictCell->isComplete())                       (load)     *)
(*   "sd"     return CAS(cbo, conflictCell, null) ? RESUME : SUSPEND       *)
(*   pass 1 (Si = Sn; Brent checkpoint):                                   *)
(*   "p1a" Ci = Si->getBlockingCell(); if(!Ci) SUSPEND          (load)     *)
(*   "p1b" Si = Ci->getOwner();        if(!Si) SUSPEND          (load)     *)
(*   "p1c" Li = Si->age; if odd SUSPEND; if(Si==checkpoint) SUSPEND;       *)
(*         push C,S,L; checkpoint update; loop while Si != Sn   (load)     *)
(*   pass 2 (Si = Sn; i = 0..n-1):                                         *)
(*   "p2a" Ci = Si->getBlockingCell()                           (load)     *)
(*   "p2b" if(Si->age != L[i-1]) SUSPEND; if(Ci != C[i]) SUSPEND (load)    *)
(*   "p2c" Si = Ci->getOwner(); if(Si != S[i]) SUSPEND; minPtr  (load)     *)
(*   "fin" if(Sn->getBlockingCell() != conflictCell ||                     *)
(*            Sn_Age != L[n-1]) SUSPEND                         (load)     *)
(*   "cas" if(!S[minPtr]->age.CAS(L[minPtr], L[minPtr]+1)) SUSPEND (RMW)   *)
(*   "rm"  Sn->removeCellBlockedOn()                    (plain store)      *)
(*   transfer, for each member S[i], i < n (transferCells):                *)
(*   "xpick" choose `last` (rank rule is abstract), move stacks (owner-only)*)
(*   "xc"  cell->transfer(delta, Sn): status = Sn, one store per cell     *)
(*   "xb"  src->suspendOn(last)  or  src->removeCellBlockedOn() (store)    *)
(*   "inc" ++S[minPtr]->age                                     (RMW)      *)
(*   "recl" for done members: reclaim -> age += 2               (RMW)      *)
(*   then RESUME.                                                          *)
(*                                                                         *)
(* ABSTRACTIONS (and what this model CANNOT show):                         *)
(*  C1 Each search starts owning CellsPer cells (<<lo, hi>> = bottom, top *)
(*     of its Tarjan stack, or <<lo>>); no new cells are claimed.  Which cell a running      *)
(*     search suspends on is arbitrary (any cell of another live search),  *)
(*     so all wait-for graphs over N searches arise, but graph structure,  *)
(*     indices and ranks are not modelled: the transfer's rank rule is     *)
(*     replaced by a nondeterministic choice of `last` at or below the     *)
(*     conflict cell.  The model therefore says NOTHING about whether the  *)
(*     transferred cells are the right ones (Tarjan correctness).          *)
(*  C2 A running search completes its top cell (one cell at a time) and    *)
(*     then CASes cellBlockedOn of every search, one step per search: an   *)
(*     abstraction of the blocked-list scan whose own correctness is       *)
(*     Suspension.tla.  The blocked-list push in "sb"/"xb" is not modelled.*)
(*  C3 Search ids 1..N, ordered by <, stand for std::less<Search*>.        *)
(*     Searches are reclaimed (age += 2) but not reused for new roots.     *)
(*  C4 Cells are never recycled (true at 2a280d9).                         *)
(*  C5 MaxSusp bounds suspensions; small N (2 or 3) only.                  *)
(*                                                                         *)
(* Mutations (FALSE in the real code):                                     *)
(*   NoConsensus     skip the odd-age CAS (every detector resolves)        *)
(*   NoSecondPass    resolve right after pass 1                            *)
(*   WalkBeforePublish  walk BEFORE publishing cellBlockedOn (publish,     *)
(*                   re-check and CAS only on the SUSPEND path)            *)
(***************************************************************************)
EXTENDS Naturals, Sequences, FiniteSets

CONSTANTS N, MaxSusp, NoConsensus, NoSecondPass, WalkBeforePublish,
          CellsPer,  \* 2: each search starts with <<lo, hi>>; 1: with <<lo>> only
          B9d4Walk   \* TRUE: b9d4ba4's pass 1, which re-reads Si->getBlockingCell()
                     \* after Ci (`if(Si->getBlockingCell() != Ci) return SUSPEND;`).
                     \* Its Ci->age reads are elided: cells are not recycled here.

ASSUME CellsPer \in {1, 2} /\ B9d4Walk \in BOOLEAN

Searches == 1..N
Workers  == 1..N
Cells    == 1..(CellsPer * N)
Lo(s) == CellsPer * (s - 1) + 1
Hi(s) == CellsPer * s
None == 0                    \* null search / null cell / COMPLETE owner

VARIABLES
  owner,    \* owner[c]: search whose stack holds c, or None if complete
  stack,    \* stack[s]: Tarjan stack (owner-only data)
  cbo,      \* cbo[s]: cellBlockedOn
  age,      \* age[s]
  sdone,    \* sdone[s]: finished and reclaimed
  pending,  \* Pending queue
  budget,
  pc, cur, cc, snAge,        \* worker: pc, search Sn, conflictCell, Sn_Age
  S, C, L,                   \* path vectors (sequences, 1-based)
  si, ci, ix, mp, chk, nchk, \* walk locals
  xi, xcells, xlast, xdone,  \* transfer locals
  ct, ccell                  \* completer locals

vars == <<owner, stack, cbo, age, sdone, pending, budget, pc, cur, cc, snAge,
          S, C, L, si, ci, ix, mp, chk, nchk, xi, xcells, xlast, xdone, ct, ccell>>

GlobalVars == <<owner, stack, cbo, age, sdone, pending, budget>>
LocalVars  == <<pc, cur, cc, snAge, S, C, L, si, ci, ix, mp, chk, nchk, xi, xcells,
                xlast, xdone, ct, ccell>>

WalkPCs  == {"p1a", "p1r", "p1b", "p1c", "p2a", "p2b", "p2c", "fin", "cas"}
ResolvePCs == {"rm", "xpick", "xc", "xb", "inc", "recl"}
SuspendPCs == {"sa", "sb", "sc", "sd"} \cup WalkPCs \cup ResolvePCs

\* The worker owns the right to run its search (cellBlockedOn not published).
Holds(w) ==
  \/ pc[w] \in {"run", "cmp", "sa"}
  \/ pc[w] = "sb" /\ ~WalkBeforePublish
  \/ pc[w] \in ResolvePCs
  \/ WalkBeforePublish /\ pc[w] \in WalkPCs \cup {"sb"}

Tok(s) == Cardinality({w \in Workers : cur[w] = s /\ Holds(w)})
          + (IF s \in pending THEN 1 ELSE 0)

Range(q) == {q[i] : i \in 1..Len(q)}
Members(w) == Range(S[w])
Resolving(w) == pc[w] \in ResolvePCs

\* Sn's own cellBlockedOn as seen by its walker (mutation: not yet published)
ReadCbo(w, x) == IF WalkBeforePublish /\ x = cur[w] THEN cc[w] ELSE cbo[x]

Init ==
  /\ owner = [c \in Cells |-> (c + CellsPer - 1) \div CellsPer]
  /\ stack = [s \in Searches |-> IF CellsPer = 2 THEN <<Lo(s), Hi(s)>> ELSE <<Lo(s)>>]
  /\ cbo = [s \in Searches |-> None]
  /\ age = [s \in Searches |-> 0]
  /\ sdone = [s \in Searches |-> FALSE]
  /\ pending = {}
  /\ budget = MaxSusp
  /\ pc = [w \in Workers |-> "run"]
  /\ cur = [w \in Workers |-> w]
  /\ cc = [w \in Workers |-> None]
  /\ snAge = [w \in Workers |-> 0]
  /\ S = [w \in Workers |-> <<>>]
  /\ C = [w \in Workers |-> <<>>]
  /\ L = [w \in Workers |-> <<>>]
  /\ si = [w \in Workers |-> None]
  /\ ci = [w \in Workers |-> None]
  /\ ix = [w \in Workers |-> 0]
  /\ mp = [w \in Workers |-> 1]
  /\ chk = [w \in Workers |-> None]
  /\ nchk = [w \in Workers |-> 1]
  /\ xi = [w \in Workers |-> 0]
  /\ xcells = [w \in Workers |-> <<>>]
  /\ xlast = [w \in Workers |-> None]
  /\ xdone = [w \in Workers |-> {}]
  /\ ct = [w \in Workers |-> 0]
  /\ ccell = [w \in Workers |-> None]

\* Reset every walk/transfer local of w (keeps the state space small).
ClearWalk(w) ==
  /\ S' = [S EXCEPT ![w] = <<>>] /\ C' = [C EXCEPT ![w] = <<>>]
  /\ L' = [L EXCEPT ![w] = <<>>]
  /\ si' = [si EXCEPT ![w] = None] /\ ci' = [ci EXCEPT ![w] = None]
  /\ ix' = [ix EXCEPT ![w] = 0] /\ mp' = [mp EXCEPT ![w] = 1]
  /\ chk' = [chk EXCEPT ![w] = None] /\ nchk' = [nchk EXCEPT ![w] = 1]
  /\ xi' = [xi EXCEPT ![w] = 0] /\ xcells' = [xcells EXCEPT ![w] = <<>>]
  /\ xlast' = [xlast EXCEPT ![w] = None] /\ xdone' = [xdone EXCEPT ![w] = {}]

UnchangedWalk == UNCHANGED <<S, C, L, si, ci, ix, mp, chk, nchk, xi, xcells, xlast, xdone>>

\* suspend() returns SUSPEND (mutation WalkBeforePublish: publish first)
WalkSuspend(w) ==
  /\ IF WalkBeforePublish
     THEN /\ pc' = [pc EXCEPT ![w] = "sb"]
          /\ UNCHANGED <<cur, cc, snAge>>
     ELSE /\ pc' = [pc EXCEPT ![w] = "idle"]
          /\ cur' = [cur EXCEPT ![w] = None]
          /\ cc' = [cc EXCEPT ![w] = None]
          /\ snAge' = [snAge EXCEPT ![w] = 0]
  /\ ClearWalk(w)

Return(w) ==            \* SUSPEND from the hand-shake steps
  /\ pc' = [pc EXCEPT ![w] = "idle"]
  /\ cur' = [cur EXCEPT ![w] = None]
  /\ cc' = [cc EXCEPT ![w] = None]
  /\ snAge' = [snAge EXCEPT ![w] = 0]
  /\ UnchangedWalk

-----------------------------------------------------------------------------
(* Running searches                                                        *)

Dequeue(w) ==
  /\ pc[w] = "idle"
  /\ \E s \in pending :
       /\ pending' = pending \ {s}
       /\ cur' = [cur EXCEPT ![w] = s]
       /\ pc' = [pc EXCEPT ![w] = "run"]
  /\ UNCHANGED <<owner, stack, cbo, age, sdone, budget, cc, snAge, ct, ccell>>
  /\ UnchangedWalk

\* buildSCC / buildSingletonSCC for the top cell: markComplete (store)
CompleteTop(w) ==
  /\ pc[w] = "run"
  /\ LET s == cur[w] IN
     /\ stack[s] # <<>>
     /\ LET c == stack[s][Len(stack[s])] IN
        /\ owner' = [owner EXCEPT ![c] = None]
        /\ stack' = [stack EXCEPT ![s] = SubSeq(stack[s], 1, Len(stack[s]) - 1)]
        /\ ccell' = [ccell EXCEPT ![w] = c]
  /\ ct' = [ct EXCEPT ![w] = 1]
  /\ pc' = [pc EXCEPT ![w] = "cmp"]
  /\ UNCHANGED <<cbo, age, sdone, pending, budget, cur, cc, snAge>>
  /\ UnchangedWalk

\* resumeAllBlockedOn: one CAS(t->cellBlockedOn, cell, null) per search (C2);
\* afterwards the search continues, or ends (reclaim: age += 2).
Resume(w) ==
  /\ pc[w] = "cmp"
  /\ IF ct[w] <= N
     THEN LET t == ct[w] IN
          /\ IF cbo[t] = ccell[w]
             THEN /\ cbo' = [cbo EXCEPT ![t] = None]
                  /\ pending' = pending \cup {t}
             ELSE UNCHANGED <<cbo, pending>>
          /\ ct' = [ct EXCEPT ![w] = ct[w] + 1]
          /\ UNCHANGED <<pc, cur, age, sdone, ccell>>
     ELSE /\ ct' = [ct EXCEPT ![w] = 0]
          /\ ccell' = [ccell EXCEPT ![w] = None]
          /\ IF stack[cur[w]] = <<>>
             THEN /\ age' = [age EXCEPT ![cur[w]] = age[cur[w]] + 2]
                  /\ sdone' = [sdone EXCEPT ![cur[w]] = TRUE]
                  /\ pc' = [pc EXCEPT ![w] = "idle"]
                  /\ cur' = [cur EXCEPT ![w] = None]
             ELSE /\ pc' = [pc EXCEPT ![w] = "run"]
                  /\ UNCHANGED <<age, sdone, cur>>
          /\ UNCHANGED <<cbo, pending>>
  /\ UNCHANGED <<owner, stack, budget, cc, snAge>>
  /\ UnchangedWalk

\* claim() returned OCCUPIED for a cell of another live search
BeginSuspend(w) ==
  /\ pc[w] = "run"
  /\ budget > 0
  /\ \E c \in Cells :
       /\ owner[c] \notin {None, cur[w]}
       /\ cc' = [cc EXCEPT ![w] = c]
  /\ budget' = budget - 1
  /\ pc' = [pc EXCEPT ![w] = "sa"]
  /\ UNCHANGED <<owner, stack, cbo, age, sdone, pending, cur, snAge, ct, ccell>>
  /\ UnchangedWalk

\* "sa": Sn_Age = Sn->age
LoadSnAge(w) ==
  /\ pc[w] = "sa"
  /\ snAge' = [snAge EXCEPT ![w] = age[cur[w]]]
  /\ IF WalkBeforePublish
     THEN /\ pc' = [pc EXCEPT ![w] = "p1a"]
          /\ si' = [si EXCEPT ![w] = cur[w]]
     ELSE /\ pc' = [pc EXCEPT ![w] = "sb"]
          /\ UNCHANGED si
  /\ UNCHANGED <<owner, stack, cbo, age, sdone, pending, budget, cur, cc, ct, ccell>>
  /\ UNCHANGED <<S, C, L, ci, ix, mp, chk, nchk, xi, xcells, xlast, xdone>>

\* "sb": Sn->suspendOn(conflictCell)
Publish(w) ==
  /\ pc[w] = "sb"
  /\ cbo' = [cbo EXCEPT ![cur[w]] = cc[w]]
  /\ pc' = [pc EXCEPT ![w] = "sc"]
  /\ UNCHANGED <<owner, stack, age, sdone, pending, budget, cur, cc, snAge, ct, ccell>>
  /\ UnchangedWalk

\* "sc": if(conflictCell->isComplete())
Recheck(w) ==
  /\ pc[w] = "sc"
  /\ IF owner[cc[w]] = None
     THEN /\ pc' = [pc EXCEPT ![w] = "sd"]
          /\ UNCHANGED <<cur, cc, snAge, si>>
     ELSE IF WalkBeforePublish
     THEN /\ pc' = [pc EXCEPT ![w] = "idle"]              \* already walked
          /\ cur' = [cur EXCEPT ![w] = None]
          /\ cc' = [cc EXCEPT ![w] = None]
          /\ snAge' = [snAge EXCEPT ![w] = 0]
          /\ UNCHANGED si
     ELSE /\ pc' = [pc EXCEPT ![w] = "p1a"]
          /\ si' = [si EXCEPT ![w] = cur[w]]
          /\ UNCHANGED <<cur, cc, snAge>>
  /\ UNCHANGED <<owner, stack, cbo, age, sdone, pending, budget, ct, ccell>>
  /\ UNCHANGED <<S, C, L, ci, ix, mp, chk, nchk, xi, xcells, xlast, xdone>>

\* "sd": removeCellBlockedOnWithCompareExchange(conflictCell)
SelfCas(w) ==
  /\ pc[w] = "sd"
  /\ IF cbo[cur[w]] = cc[w]
     THEN /\ cbo' = [cbo EXCEPT ![cur[w]] = None]
          /\ pc' = [pc EXCEPT ![w] = "run"]
          /\ cc' = [cc EXCEPT ![w] = None]
          /\ snAge' = [snAge EXCEPT ![w] = 0]
          /\ UNCHANGED cur
          /\ UnchangedWalk
     ELSE /\ Return(w)
          /\ UNCHANGED cbo
  /\ UNCHANGED <<owner, stack, age, sdone, pending, budget, ct, ccell>>

-----------------------------------------------------------------------------
(* Pass 1                                                                   *)

\* "p1a": Ci = Si->getBlockingCell(); if(!Ci) return SUSPEND;
P1a(w) ==
  /\ pc[w] = "p1a"
  /\ LET x == ReadCbo(w, si[w]) IN
     IF x = None
     THEN WalkSuspend(w)
     ELSE /\ ci' = [ci EXCEPT ![w] = x]
          /\ pc' = [pc EXCEPT ![w] = IF B9d4Walk THEN "p1r" ELSE "p1b"]
          /\ UNCHANGED <<cur, cc, snAge, S, C, L, si, ix, mp, chk, nchk, xi, xcells,
                         xlast, xdone>>
  /\ UNCHANGED <<owner, stack, cbo, age, sdone, pending, budget, ct, ccell>>

\* b9d4ba4 only: if(Si->getBlockingCell() != Ci) return SUSPEND;
P1r(w) ==
  /\ pc[w] = "p1r"
  /\ IF ReadCbo(w, si[w]) # ci[w]
     THEN WalkSuspend(w)
     ELSE /\ pc' = [pc EXCEPT ![w] = "p1b"]
          /\ UNCHANGED <<cur, cc, snAge>> /\ UnchangedWalk
  /\ UNCHANGED <<owner, stack, cbo, age, sdone, pending, budget, ct, ccell>>

\* "p1b": Si = Ci->getOwner(); if(!Si) return SUSPEND;
P1b(w) ==
  /\ pc[w] = "p1b"
  /\ LET o == owner[ci[w]] IN
     IF o = None
     THEN WalkSuspend(w)
     ELSE /\ si' = [si EXCEPT ![w] = o]
          /\ pc' = [pc EXCEPT ![w] = "p1c"]
          /\ UNCHANGED <<cur, cc, snAge, S, C, L, ci, ix, mp, chk, nchk, xi, xcells,
                         xlast, xdone>>
  /\ UNCHANGED <<owner, stack, cbo, age, sdone, pending, budget, ct, ccell>>

\* "p1c": Li = Si->age; odd -> SUSPEND; Si == checkpoint -> SUSPEND; push;
\*        Brent update; loop until Si == Sn
P1c(w) ==
  /\ pc[w] = "p1c"
  /\ LET li == age[si[w]] IN
     IF li % 2 = 1 \/ si[w] = chk[w]
     THEN WalkSuspend(w)
     ELSE LET S2 == Append(S[w], si[w]) IN
          /\ S' = [S EXCEPT ![w] = S2]
          /\ C' = [C EXCEPT ![w] = Append(C[w], ci[w])]
          /\ L' = [L EXCEPT ![w] = Append(L[w], li)]
          /\ IF Len(S2) = nchk[w]
             THEN /\ chk' = [chk EXCEPT ![w] = si[w]]
                  /\ nchk' = [nchk EXCEPT ![w] = 2 * nchk[w]]
             ELSE UNCHANGED <<chk, nchk>>
          /\ IF si[w] = cur[w]
             THEN IF NoSecondPass
                  THEN /\ pc' = [pc EXCEPT ![w] = "fin"]
                       /\ mp' = [mp EXCEPT ![w] =
                               CHOOSE i \in 1..Len(S2) :
                                 \A j \in 1..Len(S2) : S2[i] <= S2[j]]
                       /\ UNCHANGED si
                  ELSE /\ pc' = [pc EXCEPT ![w] = "p2a"]
                       /\ UNCHANGED <<si, mp>>
             ELSE /\ pc' = [pc EXCEPT ![w] = "p1a"]
                  /\ UNCHANGED <<si, mp>>
          /\ ix' = [ix EXCEPT ![w] = 1]
          /\ UNCHANGED <<cur, cc, snAge, ci, xi, xcells, xlast, xdone>>
  /\ UNCHANGED <<owner, stack, cbo, age, sdone, pending, budget, ct, ccell>>

-----------------------------------------------------------------------------
(* Pass 2  (si = Sn at entry; ix = 1-based i)                              *)

Prev(w) == IF ix[w] = 1 THEN Len(L[w]) ELSE ix[w] - 1

\* "p2a": Ci = Si->getBlockingCell()
P2a(w) ==
  /\ pc[w] = "p2a"
  /\ ci' = [ci EXCEPT ![w] = ReadCbo(w, si[w])]
  /\ pc' = [pc EXCEPT ![w] = "p2b"]
  /\ UNCHANGED <<owner, stack, cbo, age, sdone, pending, budget, ct, ccell, cur, cc, snAge>>
  /\ UNCHANGED <<S, C, L, si, ix, mp, chk, nchk, xi, xcells, xlast, xdone>>

\* "p2b": if(Si->age != L[i-1]) SUSPEND;  if(Ci != C[i]) SUSPEND;
P2b(w) ==
  /\ pc[w] = "p2b"
  /\ IF age[si[w]] # L[w][Prev(w)] \/ ci[w] # C[w][ix[w]]
     THEN WalkSuspend(w)
     ELSE /\ pc' = [pc EXCEPT ![w] = "p2c"]
          /\ UNCHANGED <<cur, cc, snAge>> /\ UnchangedWalk
  /\ UNCHANGED <<owner, stack, cbo, age, sdone, pending, budget, ct, ccell>>

\* "p2c": Si = Ci->getOwner(); if(Si != S[i]) SUSPEND; minPtr update
P2c(w) ==
  /\ pc[w] = "p2c"
  /\ LET o == owner[ci[w]] IN
     IF o # S[w][ix[w]]
     THEN WalkSuspend(w)
     ELSE /\ si' = [si EXCEPT ![w] = o]
          /\ mp' = [mp EXCEPT ![w] = IF o < S[w][mp[w]] THEN ix[w] ELSE mp[w]]
          /\ IF ix[w] = Len(S[w])
             THEN /\ pc' = [pc EXCEPT ![w] = "fin"]
                  /\ UNCHANGED ix
             ELSE /\ pc' = [pc EXCEPT ![w] = "p2a"]
                  /\ ix' = [ix EXCEPT ![w] = ix[w] + 1]
          /\ UNCHANGED <<cur, cc, snAge, S, C, L, ci, chk, nchk, xi, xcells, xlast, xdone>>
  /\ UNCHANGED <<owner, stack, cbo, age, sdone, pending, budget, ct, ccell>>

\* "fin": if(Sn->getBlockingCell() != conflictCell || Sn_Age != L[n-1]) SUSPEND
Final(w) ==
  /\ pc[w] = "fin"
  /\ IF ReadCbo(w, cur[w]) # cc[w] \/ snAge[w] # L[w][Len(L[w])]
     THEN WalkSuspend(w)
     ELSE /\ pc' = [pc EXCEPT ![w] = "cas"]
          /\ UNCHANGED <<cur, cc, snAge>> /\ UnchangedWalk
  /\ UNCHANGED <<owner, stack, cbo, age, sdone, pending, budget, ct, ccell>>

\* "cas": S[minPtr]->age.compare_exchange_strong(L[minPtr], L[minPtr] + 1)
Consensus(w) ==
  /\ pc[w] = "cas"
  /\ LET m == S[w][mp[w]] IN
     IF NoConsensus
     THEN /\ pc' = [pc EXCEPT ![w] = "rm"]
          /\ UNCHANGED age
          /\ UNCHANGED <<cur, cc, snAge>> /\ UnchangedWalk
     ELSE IF age[m] = L[w][mp[w]]
     THEN /\ age' = [age EXCEPT ![m] = age[m] + 1]
          /\ pc' = [pc EXCEPT ![w] = "rm"]
          /\ UNCHANGED <<cur, cc, snAge>> /\ UnchangedWalk
     ELSE /\ WalkSuspend(w)
          /\ UNCHANGED age
  /\ UNCHANGED <<owner, stack, cbo, sdone, pending, budget, ct, ccell>>

-----------------------------------------------------------------------------
(* Resolution                                                              *)

\* "rm": Sn->removeCellBlockedOn()  (plain store)
RemoveCbo(w) ==
  /\ pc[w] = "rm"
  /\ cbo' = [cbo EXCEPT ![cur[w]] = None]
  /\ xi' = [xi EXCEPT ![w] = 1]
  /\ pc' = [pc EXCEPT ![w] = IF Len(S[w]) > 1 THEN "xpick" ELSE "inc"]
  /\ UNCHANGED <<owner, stack, age, sdone, pending, budget, ct, ccell, cur, cc, snAge>>
  /\ UNCHANGED <<S, C, L, si, ci, ix, mp, chk, nchk, xcells, xlast, xdone>>

Pos(q, x) == CHOOSE i \in 1..Len(q) : q[i] = x

\* transferCells(S[xi], Sn, C[xi]): choose `last` at or below C[xi] (C1);
\* move the stack segment (owner-only data).
XPick(w) ==
  /\ pc[w] = "xpick"
  /\ LET src == S[w][xi[w]]  sn == cur[w]  p == Pos(stack[src], C[w][xi[w]]) IN
     \E j \in 1..p :
       LET seg == SubSeq(stack[src], j, Len(stack[src])) IN
       /\ stack' = [stack EXCEPT ![src] = SubSeq(stack[src], 1, j - 1),
                                 ![sn] = stack[sn] \o seg]
       /\ xcells' = [xcells EXCEPT ![w] = seg]
       /\ xlast' = [xlast EXCEPT ![w] = stack[src][j]]
  /\ pc' = [pc EXCEPT ![w] = "xc"]
  /\ UNCHANGED <<owner, cbo, age, sdone, pending, budget, ct, ccell, cur, cc, snAge>>
  /\ UNCHANGED <<S, C, L, si, ci, ix, mp, chk, nchk, xi, xdone>>

\* cell->transfer(delta, Sn): status = Sn, one cell per step (bottom first)
XCell(w) ==
  /\ pc[w] = "xc"
  /\ owner' = [owner EXCEPT ![Head(xcells[w])] = cur[w]]
  /\ xcells' = [xcells EXCEPT ![w] = Tail(xcells[w])]
  /\ pc' = [pc EXCEPT ![w] = IF Len(xcells[w]) = 1 THEN "xb" ELSE "xc"]
  /\ UNCHANGED <<stack, cbo, age, sdone, pending, budget, ct, ccell, cur, cc, snAge>>
  /\ UNCHANGED <<S, C, L, si, ci, ix, mp, chk, nchk, xi, xlast, xdone>>

\* src->suspendOn(last)  or (empty stack) src->removeCellBlockedOn()
XBlock(w) ==
  /\ pc[w] = "xb"
  /\ LET src == S[w][xi[w]] IN
     IF stack[src] # <<>>
     THEN /\ cbo' = [cbo EXCEPT ![src] = xlast[w]]
          /\ UNCHANGED xdone
     ELSE /\ cbo' = [cbo EXCEPT ![src] = None]
          /\ xdone' = [xdone EXCEPT ![w] = xdone[w] \cup {src}]
  /\ xi' = [xi EXCEPT ![w] = xi[w] + 1]
  /\ xlast' = [xlast EXCEPT ![w] = None]
  /\ pc' = [pc EXCEPT ![w] = IF xi[w] + 1 < Len(S[w]) THEN "xpick" ELSE "inc"]
  /\ UNCHANGED <<owner, stack, age, sdone, pending, budget, ct, ccell, cur, cc, snAge>>
  /\ UNCHANGED <<S, C, L, si, ci, ix, mp, chk, nchk, xcells>>

\* "inc": ++S[minPtr]->age
AgeEven(w) ==
  /\ pc[w] = "inc"
  /\ IF NoConsensus
     THEN UNCHANGED age
     ELSE age' = [age EXCEPT ![S[w][mp[w]]] = age[S[w][mp[w]]] + 1]
  /\ pc' = [pc EXCEPT ![w] = "recl"]
  /\ UNCHANGED <<owner, stack, cbo, sdone, pending, budget, ct, ccell, cur, cc, snAge>>
  /\ UnchangedWalk

\* "recl": reclaim each done member (age += 2), one per step; then RESUME
Reclaim(w) ==
  /\ pc[w] = "recl"
  /\ IF xdone[w] # {}
     THEN \E m \in xdone[w] :
            /\ age' = [age EXCEPT ![m] = age[m] + 2]
            /\ sdone' = [sdone EXCEPT ![m] = TRUE]
            /\ xdone' = [xdone EXCEPT ![w] = xdone[w] \ {m}]
            /\ UNCHANGED <<pc, cc, snAge, S, C, L, si, ci, ix, mp, chk, nchk, xi,
                           xcells, xlast>>
     ELSE /\ pc' = [pc EXCEPT ![w] = "run"]                 \* RESUME
          /\ cc' = [cc EXCEPT ![w] = None]
          /\ snAge' = [snAge EXCEPT ![w] = 0]
          /\ ClearWalk(w)
          /\ UNCHANGED <<age, sdone>>
  /\ UNCHANGED <<owner, stack, cbo, pending, budget, ct, ccell, cur>>

-----------------------------------------------------------------------------
Step(w) ==
  \/ Dequeue(w) \/ CompleteTop(w) \/ Resume(w) \/ BeginSuspend(w)
  \/ LoadSnAge(w) \/ Publish(w) \/ Recheck(w) \/ SelfCas(w)
  \/ P1a(w) \/ P1r(w) \/ P1b(w) \/ P1c(w) \/ P2a(w) \/ P2b(w) \/ P2c(w) \/ Final(w)
  \/ Consensus(w) \/ RemoveCbo(w) \/ XPick(w) \/ XCell(w) \/ XBlock(w)
  \/ AgeEven(w) \/ Reclaim(w)

Next == \E w \in Workers :
  \/ Dequeue(w) \/ CompleteTop(w) \/ Resume(w) \/ BeginSuspend(w)
  \/ LoadSnAge(w) \/ Publish(w) \/ Recheck(w) \/ SelfCas(w)
  \/ P1a(w) \/ P1r(w) \/ P1b(w) \/ P1c(w) \/ P2a(w) \/ P2b(w) \/ P2c(w) \/ Final(w)
  \/ Consensus(w) \/ RemoveCbo(w) \/ XPick(w) \/ XCell(w) \/ XBlock(w)
  \/ AgeEven(w) \/ Reclaim(w)

Spec == Init /\ [][Next]_vars /\ \A w \in Workers : WF_vars(Step(w))

-----------------------------------------------------------------------------
(* Properties                                                              *)

\* At most one party may run a search.
NoDoubleRun == \A s \in Searches : Tok(s) <= 1

\* At most one thread resolves a given cycle at a time: two resolvers never
\* share a member.
OneResolver ==
  \A w1, w2 \in Workers :
    (w1 # w2 /\ Resolving(w1) /\ Resolving(w2)) => Members(w1) \cap Members(w2) = {}

\* While a cycle is being resolved, nobody else may run a member, and no
\* completer CAS has resumed one (that would put it in Pending).
MembersFrozen ==
  \A w \in Workers :
    Resolving(w) => \A m \in Members(w) \ {cur[w]} : Tok(m) = 0

\* The resolver's own search is not runnable elsewhere.
ResolverExclusive ==
  \A w \in Workers : Resolving(w) => Tok(cur[w]) = 1

\* Wait-for graph over searches whose suspend() has returned.
Blk(s) == cbo[s] # None /\ Tok(s) = 0
NextS(s) == IF cbo[s] # None THEN owner[cbo[s]] ELSE None
InSuspend(s) == \E w \in Workers : cur[w] = s /\ pc[w] \in SuspendPCs
BeingResolved(s) == \E w \in Workers : Resolving(w) /\ s \in Members(w)
FreeBlk(s) == Blk(s) /\ ~InSuspend(s) /\ ~BeingResolved(s)

RECURSIVE ReachF(_, _, _)
ReachF(x, s, n) ==
  IF n = 0 \/ x = None THEN FALSE
  ELSE IF ~FreeBlk(x) THEN FALSE
  ELSE IF x = s THEN TRUE
  ELSE ReachF(NextS(x), s, n - 1)

\* Deadlock freedom: no blocking cycle among searches whose suspend() has
\* returned, with nobody inside suspend() or a transfer for any of them.
NoStuckCycle == \A s \in Searches : ~(FreeBlk(s) /\ ReachF(NextS(s), s, N))

\* Liveness: every search eventually finishes.
AllFinish == <>(\A s \in Searches : sdone[s])

-----------------------------------------------------------------------------
(* Coverage witnesses (EXPECTED TO BE VIOLATED: the situation is reachable) *)

\* Some cycle gets resolved.
W_Resolves == \A w \in Workers : pc[w] # "rm"
\* Two workers are both past pass 1 on cycles through the same search.
W_ConcurrentDetect ==
  ~\E w1, w2 \in Workers : w1 # w2 /\ pc[w1] = "cas" /\ pc[w2] = "cas"
\* A walker meets an odd age (cycle being resolved).
W_OddAge == ~\E w \in Workers : pc[w] = "p1c" /\ age[si[w]] % 2 = 1
\* A transferred member stays blocked on a cell the resolver now owns.
W_BlockedOnResolver ==
  ~\E w \in Workers, m \in Searches :
     pc[w] = "inc" /\ m \in Members(w) /\ m # cur[w] /\ cbo[m] # None /\ owner[cbo[m]] = cur[w]

=============================================================================
