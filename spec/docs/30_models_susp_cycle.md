## Model 3: `Suspension.tla`, the suspend/complete hand-shake

**Code mapped** (2a280d9; b9d4ba4 is identical apart from the embedded list, `LazyList = FALSE`):

| Action | Code |
| --- | --- |
| `BeginSuspend` | `worker.cpp:61`: `claim()` returned OCCUPIED, so `suspend()` is called |
| `LLoad` / `LCas` | `cell.hpp:128,131` `addToBlockedList`: load the list pointer, CAS(null → fresh) |
| `Inc` / `Store` | `blocked_list.hpp:141,149` `idx = head++`, `firstBuffer[idx].store(Sn)` |
| `SetBlk` | `suspension_manager.cpp:100` `Sn->suspendOn(conflictCell)` (`search.hpp:46`) |
| `Check` | `:113` `conflictCell->isComplete()`. Not complete gives SUSPEND here; the walk is Model 4 |
| `Cas` | `:114` `removeCellBlockedOnWithCompareExchange` gives RESUME or SUSPEND |
| `Mark` | `worker.cpp:117-121,135` `markComplete()`; every cell of the SCC is marked before any list is scanned |
| `LPtr` / `Size1` | `parallel_tarjan.hpp:86` `if(suspendedList && suspendedList->size())` |
| `Size2` | `suspension_manager.cpp:326` `size = blockedList->size()` |
| `ELoad` / `ECas` | `:337-338` entry load, then CAS of the entry's `cellBlockedOn` |
| `Enq` | `parallel_tarjan.hpp:93` `addPending`, one search per step |
| `RStart`…`RStore` | `search.cpp:126-127` `src->suspendOn(last)` (plain store), then `last->addToBlockedList(src)` |
| `RDone` | `search.cpp:130` `src->removeCellBlockedOn()` plus reclaim |
| `Finish` / `Start` | `worker.cpp:85` `reclaim` (`search.hpp:73` `age += 2`) and reuse via `allocateSpareSearch` |

**Abstractions.**
- S1. The searches that own the cells are not modeled. Each SCC of cells has a completer process that may start at any time.
- S2. Only the small-list path (fewer than 8 entries) is modeled, and `Cap` bounds the pushes per cell. `BlockedListIt` is not modeled.
- S3. A transfer may release any suspended search whose blocking cell `c` and target cell `last` are both incomplete and whose SCC completers have not started. That is what a confirmed cycle guarantees: `c`'s owner is suspended, and `last` is owned by the resolver's search, which runs only afterwards. While the transfer runs, those SCCs cannot start completing.
- S4. Any idle worker may dequeue, or reuse a free Search object.
- S5. `activeSearches` is omitted.

**Stale entries are reachable.** The coverage witnesses below are invariants that are *expected* to
fail; each failure is a trace proving the situation occurs in the real-code model.
- `W_StaleEntryCas`: a completer CASes a stale entry.
- `W_TwoEntriesOneSuspension`: a recycled search's stale entry sits in the same list as its live suspension.
- `W_CasRace`: the completer and the suspender race on one `cellBlockedOn`.
- `W_NullListRace`: a completer reads a null lazy list while a suspender is allocating it.
- `W_TransferBeforeRecheck`: a transfer releases a search whose suspender has not yet re-checked.

**Properties.**
- `Tok(s)` counts the parties entitled to run `s`: a worker before its `suspendOn` store, or after a successful self-CAS; the Pending copies; completers' `toResume` entries.
- `NoDoubleResume`: `Tok(s) <= 1`.
- `NoTwoRunners`, `OwnedUnblocked`, `FreeIdle`: consistency checks.
- `NoLostWakeup` (safety): no search stays blocked on a complete cell after the cell's scan finished, unless its own suspender still has to re-check.
- `EventuallyResumed` (liveness): `cbo[s] = c /\ c complete ~> cbo[s] = NULL /\ Tok(s) = 1`.

**Mutations.**

| Switch | Change | Caught by |
| --- | --- | --- |
| `NoRecheck` | no `isComplete` re-check | `NoLostWakeup` |
| `BlindResume` | resume without CAS | `NoDoubleResume` |
| `CheckThenStore` | load/compare/store instead of CAS | `NoDoubleResume` |
| `RelaxedEntry` | the entry store becomes visible only later | `NoLostWakeup` |

`RelaxedEntry` approximates the original `memory_order_relaxed` publication, which audit §6 repaired;
it covers a subset of what C++ allows. Its trace: the suspender stores the entry but it is not yet
visible, then publishes `cellBlockedOn`, then sees the cell open and SUSPENDs. The completer marks
the cell complete, reads `head = 1`, and reads a null entry.

## Model 4: `CycleDetection.tla`, cycle walk, consensus and transfer at 2a280d9

**Code mapped** (`src/parallel/suspension_manager.cpp` at 2a280d9):

| Action | Code |
| --- | --- |
| `LoadSnAge` | `:95` |
| `Publish` | `:97-100` |
| `Recheck` / `SelfCas` | `:113-114` |
| `P1a` / `P1b` / `P1c` | pass 1 `:150` `getBlockingCell`, `:154` `getOwner`, `:158-166` age load, odd-age return and Brent checkpoint, `:174` checkpoint update |
| `P2a` / `P2b` / `P2c` | pass 2 `:189`, `:193` age check (load), `:199` `getOwner`, `:206` `std::less` minPtr |
| `Final` | `:211` |
| `Consensus` | `:220` CAS on `S[minPtr]->age` |
| `RemoveCbo` | `:228` plain store |
| `XPick` / `XCell` / `XBlock` | `runCellTransfer` `:292` → `search.cpp:104` `cell->transfer` (one `status` store per cell), `:126/:130` plain stores |
| `AgeEven` | `:233` |
| `Reclaim` | `:237` |
| `CompleteTop` / `Resume` | completion: `worker.cpp:117-136`, `resumeAllBlockedOn` |

**Abstractions (C1-C5)** and what they cannot show:
- C1. Each search starts with `CellsPer` cells: `<<lo, hi>>` or `<<lo>>`. Which cell a running search suspends on is arbitrary (any cell of another live search), so every wait-for graph over N searches arises. Graph edges, indices and ranks are **not** modeled: the transfer's rank rule is replaced by a free choice of `last` at or below the conflict cell. **The model says nothing about whether the right cells are transferred (Tarjan correctness).** It checks only the concurrency protocol.
- C2. A running search completes its top cell, then CASes every search's `cellBlockedOn`, one step per search. This abstracts the blocked-list scan whose correctness is Model 3. The blocked-list pushes are not modeled.
- C3. Search ids `1..N` ordered by `<` stand in for `std::less<Search*>`. Reclaimed searches are not reused for new roots, so ABA on recycled Search objects is exercised only in Model 3.
- C4. Cells are never recycled, as at 2a280d9.
  - b9d4ba4's walk also read cell ages and re-read `cellBlockedOn` in pass 1.
  - The re-read is covered by `B9d4Walk = TRUE` (see the results).
  - Cell-age checks under cell recycling (b9d4ba4) are **not** modeled.
- C5. Bounded: N ≤ 3 and `MaxSusp` ≤ 3.

**Properties.**
- `NoDoubleRun`: `Tok(s) <= 1`.
- `OneResolver`: two concurrent resolvers never share a member.
- `MembersFrozen`: while a cycle is resolved, no member is runnable elsewhere and no completer CAS has resumed one.
- `ResolverExclusive`: the resolver's own search runs nowhere else.
- `NoStuckCycle` (deadlock freedom): no wait-for cycle among searches whose `suspend()` has returned while nobody is inside `suspend()` or a transfer for any of them.
- `AllFinish` (liveness): every search eventually finishes.

**Mutations.**

| Switch | Change | Caught by |
| --- | --- | --- |
| `NoConsensus` | no odd-age CAS | `OneResolver` |
| `WalkBeforePublish` | walk before publishing `cellBlockedOn` | `NoStuckCycle`: the two closers miss each other |
| `NoSecondPass` | no second pass | `MembersFrozen` |

**Coverage witnesses (expected to fail).**
- `W_Resolves`: a cycle is resolved.
- `W_ConcurrentDetect`: two workers reach the consensus CAS at once.
- `W_OddAge`: a walker meets an odd age.
- `W_BlockedOnResolver`: a transferred member ends up blocked on a cell the resolver now owns.
