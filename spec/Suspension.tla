------------------------------ MODULE Suspension ------------------------------
(***************************************************************************)
(* The suspend / complete hand-shake (lost-wakeup and double-resume        *)
(* freedom) of the parallel Tarjan implementation.                         *)
(*                                                                         *)
(* Code mapped (2a280d9 paths; b9d4ba4 is identical except that the list   *)
(* is embedded in the cell, i.e. LazyList = FALSE):                        *)
(*  Suspender, SuspensionManager::suspend (suspension_manager.cpp):        *)
(*    conflictCell->addToBlockedList(Sn)   cell.hpp:                       *)
(*        list = blockedSearches.load(); if(!list) CAS(null -> fresh);     *)
(*        list->push_back(Sn)   blocked_list.hpp: idx = head++;            *)
(*                                             firstBuffer[idx].store(Sn)  *)
(*    Sn->suspendOn(conflictCell)          cellBlockedOn = cell (store)    *)
(*    if(conflictCell->isComplete())       status load                     *)
(*       return CAS(cellBlockedOn, cell, null) ? RESUME : SUSPEND;         *)
(*    ...cycle walk...  (this model: SUSPEND; see CycleDetection.tla)      *)
(*  Completer, Worker::buildSCC (worker.cpp): markComplete every cell of   *)
(*    the SCC, then for each cell resumeAllBlockedOn (parallel_tarjan.hpp):*)
(*        list = getBlockedList(); if(list && list->size()) {              *)
(*          bulkUnsuspend: size = list->size();                            *)
(*            for e < size: s = firstBuffer[e].load();                     *)
(*                          if(s && CAS(s->cellBlockedOn, cell, null))     *)
(*                             toResume.push_back(s);                      *)
(*          activeSearches += k; pending.addPending(toResume) }            *)
(*  Resolver (cycle transfer), Search::transferCells (search.cpp):         *)
(*        src->suspendOn(last); last->addToBlockedList(src);   or          *)
(*        src->removeCellBlockedOn()  (stack emptied; src is reclaimed)    *)
(*  Recycling: Worker::reclaim -> Search::reset (age += 2) and the search  *)
(*        object is reused for a new root by allocateSpareSearch.          *)
(*                                                                         *)
(* All these atomics are seq_cst, so interleaving is faithful.  Each       *)
(* atomic access is one step, except: the transfer's precondition (the     *)
(* resolver saw a frozen blocking cycle) is checked when the transfer      *)
(* starts, and the activeSearches +k is omitted (Termination*.tla).        *)
(*                                                                         *)
(* ABSTRACTIONS:                                                           *)
(*  S1 The searches that own the cells are not modelled; each SCC of cells *)
(*     has one completer process that may start at any time (unless the    *)
(*     SCC is frozen by an in-progress transfer, see S3).                  *)
(*  S2 Only the small-list path (< 8 entries, firstBuffer) is modelled;    *)
(*     Cap bounds the number of pushes per cell.  BlockedListIt (>= 8      *)
(*     entries) is not modelled.                                           *)
(*  S3 A transfer is offered for any suspended search s whose blocking     *)
(*     cell c and target cell `last` are both incomplete and whose SCC     *)
(*     completers have not started.  That is what a confirmed cycle        *)
(*     guarantees (c's owner is suspended, `last` is owned by the          *)
(*     resolver's search which runs only after the transfer); while the    *)
(*     transfer is in progress those two SCCs cannot start completing.     *)
(*  S4 Workers are interchangeable: any idle worker may start a free       *)
(*     (recycled) search or dequeue a pending one.                         *)
(*  S5 suspend()'s not-complete path is SUSPEND here; RESUME after a cycle *)
(*     transfer is covered by CycleDetection.tla.                          *)
(*                                                                         *)
(* Mutations (FALSE in the real code), used to show the properties bite:   *)
(*   NoRecheck      the suspender skips the isComplete() re-check          *)
(*   BlindResume    the completer resumes every list entry without CAS     *)
(*   CheckThenStore the completer loads cellBlockedOn, compares, then      *)
(*                  stores null (two steps instead of one CAS)             *)
(*   RelaxedEntry   the list-entry store becomes visible to other threads  *)
(*                  only at some later step (approximates the original     *)
(*                  memory_order_relaxed publication, a subset of what C++ *)
(*                  allows)                                                *)
(***************************************************************************)
EXTENDS Naturals, FiniteSets, TLC

CONSTANTS Workers, Searches, Cells,
          SCCs,          \* a partition of Cells: cells completed together
          Cap,           \* max pushes per blocked list (S2)
          MaxLives,      \* max lives of one Search object (recycling)
          MaxTransfers,  \* max cycle-transfer releases
          LazyList,      \* TRUE = 2a280d9 (list allocated on first use)
          NoRecheck, BlindResume, CheckThenStore, RelaxedEntry,
          NULL

ASSUME Cap \in Nat /\ MaxLives \in Nat /\ MaxTransfers \in Nat
ASSUME UNION SCCs = Cells
ASSUME \A k1, k2 \in SCCs : k1 # k2 => k1 \cap k2 = {}

Slots == 0..(Cap - 1)

VARIABLES
  status,   \* status[c] \in {"open","complete"}  (open = owned by some search)
  lst,      \* lst[c]: blockedSearches pointer is non-null
  head,     \* head[c]: BlockedList::head
  ent,      \* ent[c][i]: firstBuffer[i]  (Searches \cup {NULL})
  buf,      \* RelaxedEntry only: stored-but-invisible entries <<c, i, s>>
  cbo,      \* cbo[s]: Search::cellBlockedOn
  free,     \* free[s]: Search object is in a recycle pool (not in use)
  lives,    \* lives[s]: how many times the object has been started
  wpc, wcur, wcell, widx,      \* workers
  pend,     \* pend[s]: copies of s in the Pending queue
  kpc, kleft, kcells, kc, ksize, ki, ke, ktr,   \* SCC completers
  scanned,  \* ghost: scanned[c] = the completer finished c's list
  rpc, rs, rc, rl, ridx,       \* the resolver (cycle transfer)
  transfers

vars == <<status, lst, head, ent, buf, cbo, free, lives, wpc, wcur, wcell, widx,
          pend, kpc, kleft, kcells, kc, ksize, ki, ke, ktr, scanned,
          rpc, rs, rc, rl, ridx, transfers>>

WPCs == {"idle", "run", "lload", "lcas", "inc", "store", "setblk", "check", "cas"}
\* Worker pcs at which the worker owns the right to run its search.
Holding == {"run", "lload", "lcas", "inc", "store", "setblk"}

RECURSIVE SumK(_, _)
SumK(s, K) == IF K = {} THEN 0
              ELSE LET k == CHOOSE x \in K : TRUE IN ktr[k][s] + SumK(s, K \ {k})

\* Number of parties that may run s: a worker holding it, Pending, and
\* completers' toResume lists.
Tok(s) == Cardinality({w \in Workers : wcur[w] = s /\ wpc[w] \in Holding})
          + pend[s] + SumK(s, SCCs)

SCCOf(c) == CHOOSE k \in SCCs : c \in k

\* Pushes onto c's list that have started but not yet done head++ (model
\* bound S2 only: keeps head <= Cap).
InFlight(c) ==
  Cardinality({w \in Workers : wcell[w] = c /\ wpc[w] \in {"lload", "lcas", "inc"}})
  + (IF rpc \in {"lload", "lcas", "inc"} /\ rl = c THEN 1 ELSE 0)
Room(c) == head[c] + InFlight(c) < Cap
Unstarted(k) == kpc[k] = "idle" /\ kleft[k] = k
Frozen(k) == rpc # "idle" /\ (rc \in k \/ rl \in k)

TypeOK ==
  /\ status \in [Cells -> {"open", "complete"}]
  /\ lst \in [Cells -> BOOLEAN]
  /\ head \in [Cells -> 0..Cap]
  /\ ent \in [Cells -> [Slots -> Searches \cup {NULL}]]
  /\ cbo \in [Searches -> Cells \cup {NULL}]
  /\ free \in [Searches -> BOOLEAN]
  /\ wpc \in [Workers -> WPCs]
  /\ pend \in [Searches -> 0..3]

Init ==
  /\ status = [c \in Cells |-> "open"]
  /\ lst = [c \in Cells |-> ~LazyList]
  /\ head = [c \in Cells |-> 0]
  /\ ent = [c \in Cells |-> [i \in Slots |-> NULL]]
  /\ buf = {}
  /\ cbo = [s \in Searches |-> NULL]
  /\ free = [s \in Searches |-> TRUE]
  /\ lives = [s \in Searches |-> 0]
  /\ wpc = [w \in Workers |-> "idle"]
  /\ wcur = [w \in Workers |-> NULL]
  /\ wcell = [w \in Workers |-> NULL]
  /\ widx = [w \in Workers |-> 0]
  /\ pend = [s \in Searches |-> 0]
  /\ kpc = [k \in SCCs |-> "idle"]
  /\ kleft = [k \in SCCs |-> k]
  /\ kcells = [k \in SCCs |-> k]
  /\ kc = [k \in SCCs |-> NULL]
  /\ ksize = [k \in SCCs |-> 0]
  /\ ki = [k \in SCCs |-> 0]
  /\ ke = [k \in SCCs |-> NULL]
  /\ ktr = [k \in SCCs |-> [s \in Searches |-> 0]]
  /\ scanned = [c \in Cells |-> FALSE]
  /\ rpc = "idle" /\ rs = NULL /\ rc = NULL /\ rl = NULL /\ ridx = 0
  /\ transfers = 0

KVars == <<kpc, kleft, kcells, kc, ksize, ki, ke, ktr, scanned>>
RVars == <<rpc, rs, rc, rl, ridx, transfers>>
WVars == <<wpc, wcur, wcell, widx>>

-----------------------------------------------------------------------------
(* Workers                                                                 *)

\* allocateSpareSearch + claimOrFail of a root: a free Search object starts
\* a new life (age += 2 happened at reclaim).
Start(w) ==
  /\ wpc[w] = "idle"
  /\ \E s \in Searches :
       /\ free[s] /\ lives[s] < MaxLives
       /\ free' = [free EXCEPT ![s] = FALSE]
       /\ lives' = [lives EXCEPT ![s] = lives[s] + 1]
       /\ wcur' = [wcur EXCEPT ![w] = s]
       /\ wpc' = [wpc EXCEPT ![w] = "run"]
  /\ UNCHANGED <<status, lst, head, ent, buf, cbo, wcell, widx, pend>>
  /\ UNCHANGED KVars /\ UNCHANGED RVars

\* getSearch: pending.get()
Dequeue(w) ==
  /\ wpc[w] = "idle"
  /\ \E s \in Searches :
       /\ pend[s] > 0
       /\ pend' = [pend EXCEPT ![s] = pend[s] - 1]
       /\ wcur' = [wcur EXCEPT ![w] = s]
       /\ wpc' = [wpc EXCEPT ![w] = "run"]
  /\ UNCHANGED <<status, lst, head, ent, buf, cbo, free, lives, wcell, widx>>
  /\ UNCHANGED KVars /\ UNCHANGED RVars

\* execute() ends: the search is reclaimed into a recycle pool.
Finish(w) ==
  /\ wpc[w] = "run"
  /\ free' = [free EXCEPT ![wcur[w]] = TRUE]
  /\ wcur' = [wcur EXCEPT ![w] = NULL]
  /\ wpc' = [wpc EXCEPT ![w] = "idle"]
  /\ UNCHANGED <<status, lst, head, ent, buf, cbo, lives, wcell, widx, pend>>
  /\ UNCHANGED KVars /\ UNCHANGED RVars

\* claim() returned OCCUPIED for cell c (it was neither NEW nor COMPLETE)
BeginSuspend(w) ==
  /\ wpc[w] = "run"
  /\ \E c \in Cells :
       /\ status[c] = "open"
       /\ Room(c)
       /\ wcell' = [wcell EXCEPT ![w] = c]
       /\ wpc' = [wpc EXCEPT ![w] = IF LazyList THEN "lload" ELSE "inc"]
  /\ UNCHANGED <<status, lst, head, ent, buf, cbo, free, lives, wcur, widx, pend>>
  /\ UNCHANGED KVars /\ UNCHANGED RVars

\* addToBlockedList: list = blockedSearches.load()
LLoad(w) ==
  /\ wpc[w] = "lload"
  /\ wpc' = [wpc EXCEPT ![w] = IF lst[wcell[w]] THEN "inc" ELSE "lcas"]
  /\ UNCHANGED <<status, lst, head, ent, buf, cbo, free, lives, wcur, wcell, widx, pend>>
  /\ UNCHANGED KVars /\ UNCHANGED RVars

\* addToBlockedList: blockedSearches.compare_exchange_strong(null, fresh)
LCas(w) ==
  /\ wpc[w] = "lcas"
  /\ lst' = [lst EXCEPT ![wcell[w]] = TRUE]     \* ours or another thread's
  /\ wpc' = [wpc EXCEPT ![w] = "inc"]
  /\ UNCHANGED <<status, head, ent, buf, cbo, free, lives, wcur, wcell, widx, pend>>
  /\ UNCHANGED KVars /\ UNCHANGED RVars

\* push_back: idx = head++
Inc(w) ==
  /\ wpc[w] = "inc"
  /\ LET c == wcell[w] IN
       /\ widx' = [widx EXCEPT ![w] = head[c]]
       /\ head' = [head EXCEPT ![c] = head[c] + 1]
  /\ wpc' = [wpc EXCEPT ![w] = "store"]
  /\ UNCHANGED <<status, lst, ent, buf, cbo, free, lives, wcur, wcell, pend>>
  /\ UNCHANGED KVars /\ UNCHANGED RVars

\* push_back: firstBuffer[idx].store(Sn)
Store(w) ==
  /\ wpc[w] = "store"
  /\ IF RelaxedEntry
     THEN /\ buf' = buf \cup {<<wcell[w], widx[w], wcur[w]>>}
          /\ UNCHANGED ent
     ELSE /\ ent' = [ent EXCEPT ![wcell[w]][widx[w]] = wcur[w]]
          /\ UNCHANGED buf
  /\ wpc' = [wpc EXCEPT ![w] = "setblk"]
  /\ UNCHANGED <<status, lst, head, cbo, free, lives, wcur, wcell, widx, pend>>
  /\ UNCHANGED KVars /\ UNCHANGED RVars

\* Sn->suspendOn(conflictCell): from here on the worker no longer owns Sn
SetBlk(w) ==
  /\ wpc[w] = "setblk"
  /\ cbo' = [cbo EXCEPT ![wcur[w]] = wcell[w]]
  /\ wpc' = [wpc EXCEPT ![w] = "check"]
  /\ UNCHANGED <<status, lst, head, ent, buf, free, lives, wcur, wcell, widx, pend>>
  /\ UNCHANGED KVars /\ UNCHANGED RVars

\* if(conflictCell->isComplete()) ... else (cycle walk) SUSPEND
Check(w) ==
  /\ wpc[w] = "check"
  /\ IF ~NoRecheck /\ status[wcell[w]] = "complete"
     THEN /\ wpc' = [wpc EXCEPT ![w] = "cas"]
          /\ UNCHANGED <<wcur, wcell>>
     ELSE /\ wpc' = [wpc EXCEPT ![w] = "idle"]                \* SUSPEND
          /\ wcur' = [wcur EXCEPT ![w] = NULL]
          /\ wcell' = [wcell EXCEPT ![w] = NULL]
  /\ UNCHANGED <<status, lst, head, ent, buf, cbo, free, lives, widx, pend>>
  /\ UNCHANGED KVars /\ UNCHANGED RVars

\* removeCellBlockedOnWithCompareExchange(conflictCell) ? RESUME : SUSPEND
Cas(w) ==
  /\ wpc[w] = "cas"
  /\ LET s == wcur[w]  c == wcell[w] IN
     IF cbo[s] = c
     THEN /\ cbo' = [cbo EXCEPT ![s] = NULL]
          /\ wpc' = [wpc EXCEPT ![w] = "run"]                 \* RESUME
          /\ UNCHANGED <<wcur, wcell>>
     ELSE /\ wpc' = [wpc EXCEPT ![w] = "idle"]                \* SUSPEND
          /\ wcur' = [wcur EXCEPT ![w] = NULL]
          /\ wcell' = [wcell EXCEPT ![w] = NULL]
          /\ UNCHANGED cbo
  /\ UNCHANGED <<status, lst, head, ent, buf, free, lives, widx, pend>>
  /\ UNCHANGED KVars /\ UNCHANGED RVars

Worker(w) ==
  \/ Start(w) \/ Dequeue(w) \/ Finish(w) \/ BeginSuspend(w) \/ LLoad(w) \/ LCas(w)
  \/ Inc(w) \/ Store(w) \/ SetBlk(w) \/ Check(w) \/ Cas(w)

-----------------------------------------------------------------------------
(* Completers: buildSCC marks every cell of the SCC, then scans each list. *)

\* Next cell to scan (or finished).  Worker-local choice, folded into the
\* step that finishes the previous cell.
NextCell(k, done) ==
  LET rest == kcells[k] \ done IN
  IF rest = {}
  THEN /\ kpc' = [kpc EXCEPT ![k] = "fin"]
       /\ kcells' = [kcells EXCEPT ![k] = {}]
       /\ kc' = [kc EXCEPT ![k] = NULL]
  ELSE \E c \in rest :
       /\ kpc' = [kpc EXCEPT ![k] = "lptr"]
       /\ kcells' = [kcells EXCEPT ![k] = rest]
       /\ kc' = [kc EXCEPT ![k] = c]

\* cell->markComplete()  (status = COMPLETE, seq_cst store)
Mark(k) ==
  /\ kpc[k] = "idle"
  /\ ~Frozen(k)
  /\ \E c \in kleft[k] :
       /\ status' = [status EXCEPT ![c] = "complete"]
       /\ kleft' = [kleft EXCEPT ![k] = kleft[k] \ {c}]
       /\ IF kleft[k] = {c}
          THEN NextCell(k, {})
          ELSE UNCHANGED <<kpc, kcells, kc>>
  /\ UNCHANGED <<lst, head, ent, buf, cbo, free, lives, pend, ksize, ki, ke, ktr, scanned>>
  /\ UNCHANGED WVars /\ UNCHANGED RVars

\* Finish the current cell's scan: mark scanned, move on.
CellDone(k) ==
  /\ scanned' = [scanned EXCEPT ![kc[k]] = TRUE]
  /\ NextCell(k, {kc[k]})

\* resumeAllBlockedOn: list = getBlockedList(); null -> resume nobody
LPtr(k) ==
  /\ kpc[k] = "lptr"
  /\ IF lst[kc[k]]
     THEN /\ kpc' = [kpc EXCEPT ![k] = "size1"]
          /\ UNCHANGED <<kcells, kc, scanned>>
     ELSE CellDone(k)
  /\ UNCHANGED <<status, lst, head, ent, buf, cbo, free, lives, pend, kleft, ksize, ki, ke, ktr>>
  /\ UNCHANGED WVars /\ UNCHANGED RVars

\* resumeAllBlockedOn: if(list->size()) ...
Size1(k) ==
  /\ kpc[k] = "size1"
  /\ IF head[kc[k]] > 0
     THEN /\ kpc' = [kpc EXCEPT ![k] = "size2"]
          /\ UNCHANGED <<kcells, kc, scanned>>
     ELSE CellDone(k)
  /\ UNCHANGED <<status, lst, head, ent, buf, cbo, free, lives, pend, kleft, ksize, ki, ke, ktr>>
  /\ UNCHANGED WVars /\ UNCHANGED RVars

\* bulkUnsuspend: const size = blockedList->size()
Size2(k) ==
  /\ kpc[k] = "size2"
  /\ ksize' = [ksize EXCEPT ![k] = head[kc[k]]]
  /\ ki' = [ki EXCEPT ![k] = 0]
  /\ kpc' = [kpc EXCEPT ![k] = "eload"]
  /\ UNCHANGED <<status, lst, head, ent, buf, cbo, free, lives, pend, kleft, kcells, kc,
                 ke, ktr, scanned>>
  /\ UNCHANGED WVars /\ UNCHANGED RVars

\* search = suspendedSearches[e].load()
ELoad(k) ==
  /\ kpc[k] = "eload"
  /\ IF ki[k] < ksize[k]
     THEN LET e == ent[kc[k]][ki[k]] IN
          IF e = NULL
          THEN /\ ki' = [ki EXCEPT ![k] = ki[k] + 1]
               /\ UNCHANGED <<kpc, ke>>
          ELSE /\ ke' = [ke EXCEPT ![k] = e]
               /\ kpc' = [kpc EXCEPT ![k] = "ecas"]
               /\ UNCHANGED ki
     ELSE /\ kpc' = [kpc EXCEPT ![k] = "enq"]
          /\ UNCHANGED <<ki, ke>>
  /\ UNCHANGED <<status, lst, head, ent, buf, cbo, free, lives, pend, kleft, kcells, kc,
                 ksize, ktr, scanned>>
  /\ UNCHANGED WVars /\ UNCHANGED RVars

\* search->removeCellBlockedOnWithCompareExchange(completeCell)
\* (mutations: BlindResume = no check; CheckThenStore = load, compare, then a
\*  separate store step "est")
ECas(k) ==
  /\ kpc[k] = "ecas"
  /\ LET s == ke[k]  c == kc[k] IN
     IF BlindResume
     THEN /\ cbo' = [cbo EXCEPT ![s] = NULL]
          /\ ktr' = [ktr EXCEPT ![k][s] = ktr[k][s] + 1]
          /\ ki' = [ki EXCEPT ![k] = ki[k] + 1]
          /\ kpc' = [kpc EXCEPT ![k] = "eload"]
     ELSE IF CheckThenStore
     THEN IF cbo[s] = c
          THEN /\ kpc' = [kpc EXCEPT ![k] = "est"]
               /\ UNCHANGED <<cbo, ktr, ki>>
          ELSE /\ ki' = [ki EXCEPT ![k] = ki[k] + 1]
               /\ kpc' = [kpc EXCEPT ![k] = "eload"]
               /\ UNCHANGED <<cbo, ktr>>
     ELSE /\ IF cbo[s] = c
             THEN /\ cbo' = [cbo EXCEPT ![s] = NULL]
                  /\ ktr' = [ktr EXCEPT ![k][s] = ktr[k][s] + 1]
             ELSE UNCHANGED <<cbo, ktr>>
          /\ ki' = [ki EXCEPT ![k] = ki[k] + 1]
          /\ kpc' = [kpc EXCEPT ![k] = "eload"]
  /\ UNCHANGED <<status, lst, head, ent, buf, free, lives, pend, kleft, kcells, kc,
                 ksize, ke, scanned>>
  /\ UNCHANGED WVars /\ UNCHANGED RVars

\* CheckThenStore mutation only: the separate store of null
EStore(k) ==
  /\ kpc[k] = "est"
  /\ cbo' = [cbo EXCEPT ![ke[k]] = NULL]
  /\ ktr' = [ktr EXCEPT ![k][ke[k]] = ktr[k][ke[k]] + 1]
  /\ ki' = [ki EXCEPT ![k] = ki[k] + 1]
  /\ kpc' = [kpc EXCEPT ![k] = "eload"]
  /\ UNCHANGED <<status, lst, head, ent, buf, free, lives, pend, kleft, kcells, kc,
                 ksize, ke, scanned>>
  /\ UNCHANGED WVars /\ UNCHANGED RVars

\* pending.addPending(toResume): one search per step; then the next cell
Enq(k) ==
  /\ kpc[k] = "enq"
  /\ IF \E s \in Searches : ktr[k][s] > 0
     THEN \E s \in Searches :
            /\ ktr[k][s] > 0
            /\ ktr' = [ktr EXCEPT ![k][s] = ktr[k][s] - 1]
            /\ pend' = [pend EXCEPT ![s] = pend[s] + 1]
            /\ UNCHANGED <<kpc, kcells, kc, scanned>>
     ELSE /\ CellDone(k)
          /\ UNCHANGED <<ktr, pend>>
  /\ UNCHANGED <<status, lst, head, ent, buf, cbo, free, lives, kleft, ksize, ki, ke>>
  /\ UNCHANGED WVars /\ UNCHANGED RVars

Completer(k) ==
  \/ Mark(k) \/ LPtr(k) \/ Size1(k) \/ Size2(k) \/ ELoad(k) \/ ECas(k) \/ EStore(k)
  \/ Enq(k)

-----------------------------------------------------------------------------
(* RelaxedEntry mutation: a buffered entry store becomes visible.          *)
Flush ==
  /\ \E x \in buf :
       /\ ent' = [ent EXCEPT ![x[1]][x[2]] = x[3]]
       /\ buf' = buf \ {x}
  /\ UNCHANGED <<status, lst, head, cbo, free, lives, pend>>
  /\ UNCHANGED WVars /\ UNCHANGED KVars /\ UNCHANGED RVars

-----------------------------------------------------------------------------
(* Resolver: Search::transferCells for one cycle member s (S3)             *)

TransferOK(s) ==
  /\ cbo[s] # NULL
  /\ Tok(s) = 0
  /\ status[cbo[s]] = "open"
  /\ Unstarted(SCCOf(cbo[s]))

\* src->suspendOn(last) (plain store), then last->addToBlockedList(src)
RStart ==
  /\ rpc = "idle"
  /\ transfers < MaxTransfers
  /\ \E s \in Searches, last \in Cells :
       /\ TransferOK(s)
       /\ last # cbo[s]
       /\ status[last] = "open" /\ Unstarted(SCCOf(last))
       /\ Room(last)
       /\ rs' = s /\ rc' = cbo[s] /\ rl' = last
       /\ cbo' = [cbo EXCEPT ![s] = last]
       /\ rpc' = IF LazyList THEN "lload" ELSE "inc"
  /\ transfers' = transfers + 1
  /\ UNCHANGED <<status, lst, head, ent, buf, free, lives, pend, ridx>>
  /\ UNCHANGED WVars /\ UNCHANGED KVars

RLLoad ==
  /\ rpc = "lload"
  /\ rpc' = IF lst[rl] THEN "inc" ELSE "lcas"
  /\ UNCHANGED <<status, lst, head, ent, buf, cbo, free, lives, pend, rs, rc, rl, ridx, transfers>>
  /\ UNCHANGED WVars /\ UNCHANGED KVars

RLCas ==
  /\ rpc = "lcas"
  /\ lst' = [lst EXCEPT ![rl] = TRUE]
  /\ rpc' = "inc"
  /\ UNCHANGED <<status, head, ent, buf, cbo, free, lives, pend, rs, rc, rl, ridx, transfers>>
  /\ UNCHANGED WVars /\ UNCHANGED KVars

RInc ==
  /\ rpc = "inc"
  /\ ridx' = head[rl]
  /\ head' = [head EXCEPT ![rl] = head[rl] + 1]
  /\ rpc' = "store"
  /\ UNCHANGED <<status, lst, ent, buf, cbo, free, lives, pend, rs, rc, rl, transfers>>
  /\ UNCHANGED WVars /\ UNCHANGED KVars

RStore ==
  /\ rpc = "store"
  /\ IF RelaxedEntry
     THEN /\ buf' = buf \cup {<<rl, ridx, rs>>}
          /\ UNCHANGED ent
     ELSE /\ ent' = [ent EXCEPT ![rl][ridx] = rs]
          /\ UNCHANGED buf
  /\ rpc' = "idle" /\ rs' = NULL /\ rc' = NULL /\ rl' = NULL
  /\ UNCHANGED <<status, lst, head, cbo, free, lives, pend, ridx, transfers>>
  /\ UNCHANGED WVars /\ UNCHANGED KVars

\* src's tarjan stack emptied: src->removeCellBlockedOn(); worker.reclaim(src)
RDone ==
  /\ rpc = "idle"
  /\ transfers < MaxTransfers
  /\ \E s \in Searches :
       /\ TransferOK(s)
       /\ cbo' = [cbo EXCEPT ![s] = NULL]
       /\ free' = [free EXCEPT ![s] = TRUE]
  /\ transfers' = transfers + 1
  /\ UNCHANGED <<status, lst, head, ent, buf, lives, pend, rpc, rs, rc, rl, ridx>>
  /\ UNCHANGED WVars /\ UNCHANGED KVars

RCont == RLLoad \/ RLCas \/ RInc \/ RStore

-----------------------------------------------------------------------------
Next ==
  \/ \E w \in Workers : Worker(w)
  \/ \E k \in SCCs : Completer(k)
  \/ RStart \/ RCont \/ RDone
  \/ Flush

Fairness ==
  /\ \A w \in Workers : WF_vars(Worker(w))
  /\ \A k \in SCCs : WF_vars(Completer(k))
  /\ WF_vars(RCont)
  /\ WF_vars(Flush)

Spec == Init /\ [][Next]_vars /\ Fairness

-----------------------------------------------------------------------------
(* Properties                                                              *)

\* Never resumed twice: at most one party may run a search at any time.
NoDoubleResume == \A s \in Searches : Tok(s) <= 1

\* Two workers never execute one search.
NoTwoRunners ==
  \A s \in Searches :
    Cardinality({w \in Workers : wcur[w] = s /\ wpc[w] \in Holding}) <= 1

\* A search that some party may run is not published as blocked.
OwnedUnblocked == \A s \in Searches : Tok(s) > 0 => cbo[s] = NULL

\* A recycled Search object is not blocked, pending, or running.
FreeIdle == \A s \in Searches : free[s] => (Tok(s) = 0 /\ cbo[s] = NULL)

\* Lost wakeup, safety form: s is still blocked on a complete cell c whose
\* list scan has finished, and no suspender of s on c is still to re-check.
NoLostWakeup ==
  \A s \in Searches, c \in Cells :
    ~ ( /\ cbo[s] = c
        /\ scanned[c]
        /\ ~\E w \in Workers : wcur[w] = s /\ wcell[w] = c /\ wpc[w] \in {"check", "cas"} )

\* Lost wakeup, liveness form: a search blocked on a complete cell is
\* eventually resumed by exactly one party.
Resumed(s) == cbo[s] = NULL /\ Tok(s) = 1
EventuallyResumed ==
  \A s \in Searches, c \in Cells :
    (cbo[s] = c /\ status[c] = "complete") ~> Resumed(s)


-----------------------------------------------------------------------------
(* Coverage witnesses.  Each is EXPECTED TO BE VIOLATED; a violation is a   *)
(* trace showing that the interesting situation is reachable, so the       *)
(* PASS results above are not vacuous.                                     *)

\* A completer is about to CAS a stale entry (the search is no longer blocked
\* on this cell: released by a transfer, resumed, or recycled).
W_StaleEntryCas ==
  ~\E k \in SCCs : kpc[k] = "ecas" /\ cbo[ke[k]] # kc[k]

\* A stale entry of a recycled search whose new life is blocked on the SAME
\* cell: two entries, one live suspension.
W_TwoEntriesOneSuspension ==
  ~\E c \in Cells, i, j \in Slots :
     i # j /\ ent[c][i] # NULL /\ ent[c][i] = ent[c][j] /\ cbo[ent[c][i]] = c

\* The completer and the suspender race to CAS the same cellBlockedOn.
W_CasRace ==
  ~\E k \in SCCs, w \in Workers :
     /\ kpc[k] = "ecas" /\ wpc[w] = "cas"
     /\ ke[k] = wcur[w] /\ kc[k] = wcell[w] /\ cbo[wcur[w]] = wcell[w]

\* A completer finds no list (lazy allocation) although a suspender is
\* registering on that cell.
W_NullListRace ==
  ~\E k \in SCCs, w \in Workers :
     /\ kpc[k] = "lptr" /\ ~lst[kc[k]] /\ wcell[w] = kc[k] /\ wpc[w] = "lcas"

\* A transfer moved a search whose own suspender has not yet re-checked.
W_TransferBeforeRecheck ==
  ~(rpc # "idle" /\ \E w \in Workers : wcur[w] = rs /\ wpc[w] = "check")

\* Symmetry set for safety-only configurations (unsound for liveness).
Symm == Permutations(Workers) \cup Permutations(Searches)

=============================================================================
