# TLA+ models of the parallel Tarjan concurrency protocols

The models cover the termination protocols (old flag word, the `activeSearches` counter at b9d4ba4,
and the root-block counter at 2a280d9), the suspend/complete hand-shake, and blocking-cycle detection
with resolver consensus. All of them are checked with TLC.

Sources were read **only from git objects** (`git show <rev>:<path>`), never from the working tree:

| Rev | Files |
| --- | --- |
| `8a9f1a9` | `Tarjan Code/multiThreadedTarjan.{cpp,hpp}`, `worker.cpp` (old flag protocol) |
| `b9d4ba4` | `Tarjan Code/multiThreadedTarjan.{cpp,hpp}`, `worker.cpp`, `suspensionManager.cpp`, `blockedList.hpp`, `cell.h`, `search.hpp`, `stealingQueue.{hpp,cpp}`, `pending.hpp`, `docs/concurrency-audit.md` |
| `2a280d9` | `src/parallel/{parallel_tarjan.cpp,parallel_tarjan.hpp,worker.hpp,worker.cpp,root_queue.hpp,pending_queue.hpp,cell.hpp,blocked_list.hpp,search.hpp,search.cpp,suspension_manager.cpp}` |

The models quote the code at those revisions. Later refactoring renamed and moved some of it
without changing the protocols:

| At 2a280d9 | Now |
| --- | --- |
| `Cell::claimOrFail`, `getOwner`, `getBlockedList` | `Cell::tryClaim`, `owner`, `blockedList` |
| `Search::getBlockingCell`, `removeCellBlockedOn`, `removeCellBlockedOnWithCompareExchange` | `Search::blockedOn`, `clearBlockedOn`, `tryClearBlockedOn` |
| `Search::pushToStacks`, `setRoot` | `Search::push` |
| `SuspensionManager::suspend`, `runCellTransfer`, `bulkUnsuspend` (`suspension_manager.cpp`) | `suspend`, `resolveCycle`, `collectResumable` (`suspension.cpp`) |
| `Search::transferCells` (`search.cpp`) | `transferCells` (`suspension.cpp`) |
| `BlockedListIt`, `isSmallList`, `getSmallBuffer` | `BlockedList::forEach` |
| `Worker::S`, `C`, `L`, `getID` | `Worker::blockingPath()`, `id` |
| `RootQueue::nextChunk`, the member `cellQueue` | `RootQueue::nextBlock`, the member `roots` |

Tools: TLC 2.19 from `tla2tools.jar` v1.7.4, the latest stable release; v1.8.0 is still a
pre-release. SHA-256 `936a262061c914694dfd669a543be24573c45d5aa0ff20a8b96b23d01e050e88`.
Java: Temurin 25 at `/usr/bin/java`.

**Semantics.** Every atomic the protocols use is sequentially consistent: default `operator=`,
`load`, `++`, `fetch_add`/`fetch_sub` and CAS. The only exceptions are `relaxed` loads that serve
as hints and are always re-validated by a seq_cst operation. Interleaving semantics is therefore
faithful. Each shared atomic access is its own TLA+ step. Owner-only, non-atomic work is folded into
one step, and every such fold is listed below.

`-deadlock` is passed to every run, because every bounded model ends in terminal states. Stuck
states are caught by explicit invariants and liveness properties instead: `NoEarlyExit`,
`AllExitedAllDone`, `NoStuckCycle`, `Termination` and `AllFinish`.

## Results at a glance

No counterexample was found in the **current** protocols: the counter termination at b9d4ba4 and
2a280d9, the suspend/complete hand-shake, and cycle detection with consensus at 2a280d9. Every PASS
holds only within the bounds in the table below. The old flag protocol (8a9f1a9) fails as the audit
predicted: (a) safety and (b) liveness counterexamples were found, plus (c) a hang that needs no
stall.

| Model | Property | Result |
| --- | --- | --- |
| `Termination` (b9d4ba4) | `CountInv`, `NoEarlyExit`, `DoneOnExit`, `AllExitedAllDone` | PASS (3 workers, 3 roots, 3 suspensions; plus 4 roots / 4 suspensions safety-only) |
| | `Termination` (liveness, WF + SF on dequeue), with **any** number of workers stalled in `sleep_for` | PASS (3 workers, 3 roots; 4 roots in the long run) |
| | mutations: enqueue before `+=k`; take root before `+=1` | both caught (`NoEarlyExit`) |
| `TerminationBlocks` (2a280d9) | `CountInv`, `BlockInv`, `NoEarlyExit`, `DoneOnExit`, `AllExitedAllDone` | PASS (3 workers, 3-4 vertices, blocks of 1 and 2) |
| | `Termination` liveness with stalls | PASS (blocks of 2; blocks of 1 in the long run) |
| | mutation: count the block after `nextChunk` | caught (`NoEarlyExit`) |
| `TerminationFlags` (8a9f1a9) | (a) `NoEarlyExit` | **FAIL**: realizable 24-state trace |
| | (b) `Termination` with 1 stalled idle worker | **FAIL**: stall-only configuration (no stale-bit route possible) |
| | (c) `Termination` with no stall | **FAIL**: stale bit from a failed root claim |
| | `AllExitedAllDone` | PASS: the early exit causes a hang or lost parallelism, not missing SCCs (in this abstraction) |
| `Suspension` (2a280d9 and b9d4ba4) | `NoDoubleResume`, `NoTwoRunners`, `OwnedUnblocked`, `FreeIdle`, `NoLostWakeup` | PASS (2 workers, 2 searches, 2 cells, lazy and embedded list, one 2-cell SCC or two 1-cell SCCs, recycling, transfers) |
| | `EventuallyResumed` (liveness) | see table (shrunk configuration) |
| | mutations: no re-check, blind resume, check-then-store, relaxed entry publication | all caught |
| | coverage: stale-entry CAS, two entries for one suspension, CAS race, null lazy list, transfer before re-check | all reachable |
| `CycleDetection` (2a280d9) | `NoDoubleRun`, `OneResolver`, `MembersFrozen`, `ResolverExclusive`, `NoStuckCycle` | PASS (N=2 with 2 cells each; N=3 with 1 cell each; N=3 with 2 cells each in the long run) |
| | `AllFinish` (liveness) | PASS (N=2, 2 cells; N=3, 1 cell) |
| | b9d4ba4 pass-1 walk (`B9d4Walk`) | see table |
| | mutations: no consensus CAS, walk before publishing, no second pass | all caught |
| | coverage: resolution, concurrent detection, odd age met, member blocked on the resolver's cell | all reachable |

## Full results

All counts come from the TLC output in `results/`. "TLC workers" is the `-workers` value of the run
that produced the file. The model was later capped at 2 workers because the machine is shared. Runs
recorded with 8 or 18 workers happened before that cap; they were not repeated if the result was
already in hand, since the worker count does not change what TLC explores. Times are wall-clock
seconds on an Apple M5 Max.

| Config | Constants | Checked | Expected | Result | Distinct states | Depth | Time (s) | TLC workers |
| --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: |
| `Termination` | Workers=3, NRoots=3, MaxSusp=3, AllowStall=FALSE, MaxStalls=3, SpuriousEmpty=TRUE | TypeOK CountInv NoEarlyExit DoneOnExit AllExitedAllDone NoSuspendedCycle Termination | pass | PASS | 465,864 | 51 | 37 | 2 |
| `Termination_stall` | Workers=3, NRoots=3, MaxSusp=3, AllowStall=TRUE, MaxStalls=3, SpuriousEmpty=TRUE | TypeOK CountInv NoEarlyExit DoneOnExit AllExitedAllDone NoSuspendedCycle Termination | pass | PASS | 634,173 | 52 | 51 | 2 |
| `Termination_stall_big` | Workers=3, NRoots=4, MaxSusp=4, AllowStall=TRUE, MaxStalls=3, SpuriousEmpty=TRUE | TypeOK CountInv NoEarlyExit DoneOnExit AllExitedAllDone NoSuspendedCycle (symmetry) | pass | PASS | 1,925,081 | 63 | 19 | 2 |
| `Termination_mut_enqueue` | Workers=3, NRoots=3, MaxSusp=3, AllowStall=FALSE, MaxStalls=3, SpuriousEmpty=TRUE | TypeOK NoEarlyExit DoneOnExit AllExitedAllDone | fail(mutation) | FAIL NoEarlyExit | 39,432 | 22 | 1 | 2 |
| `Termination_mut_take` | Workers=3, NRoots=3, MaxSusp=3, AllowStall=FALSE, MaxStalls=3, SpuriousEmpty=TRUE | TypeOK NoEarlyExit DoneOnExit AllExitedAllDone | fail(mutation) | FAIL NoEarlyExit | 756 | 10 | 1 | 2 |
| `Termination_stall_big_live` (long) | Workers=3, NRoots=4, MaxSusp=4, AllowStall=TRUE, MaxStalls=3, SpuriousEmpty=TRUE | TypeOK CountInv NoEarlyExit DoneOnExit AllExitedAllDone NoSuspendedCycle Termination | pass | not run | | | | |
| `TerminationBlocks` | Workers=3, NV=3, Chunk=2, MaxSusp=3, AllowStall=TRUE, MaxStalls=3, SpuriousEmpty=TRUE | TypeOK CountInv BlockInv NoEarlyExit DoneOnExit AllExitedAllDone NoSuspendedCycle Termination | pass | PASS | 1,145,536 | 54 | 91 | 2 |
| `TerminationBlocks_c1` | Workers=3, NV=3, Chunk=1, MaxSusp=3, AllowStall=TRUE, MaxStalls=3, SpuriousEmpty=TRUE | TypeOK CountInv BlockInv NoEarlyExit DoneOnExit AllExitedAllDone NoSuspendedCycle (symmetry) | pass | PASS | 935,781 | 58 | 11 | 2 |
| `TerminationBlocks_big` | Workers=3, NV=4, Chunk=2, MaxSusp=3, AllowStall=TRUE, MaxStalls=3, SpuriousEmpty=TRUE | TypeOK CountInv BlockInv NoEarlyExit DoneOnExit AllExitedAllDone NoSuspendedCycle (symmetry) | pass | PASS | 1,243,498 | 60 | 15 | 2 |
| `TerminationBlocks_mut_count` | Workers=3, NV=3, Chunk=2, MaxSusp=2, AllowStall=FALSE, MaxStalls=3, SpuriousEmpty=TRUE | TypeOK NoEarlyExit DoneOnExit AllExitedAllDone | fail(mutation) | FAIL NoEarlyExit | 231 | 10 | 1 | 2 |
| `TerminationBlocks_c1_live` (long) | Workers=3, NV=3, Chunk=1, MaxSusp=3, AllowStall=TRUE, MaxStalls=3, SpuriousEmpty=TRUE | TypeOK CountInv BlockInv NoEarlyExit DoneOnExit AllExitedAllDone NoSuspendedCycle Termination | pass | not run | | | | |
| `TerminationFlags_safety` | Workers=3, NRoots=3, MaxSusp=3, AllowStall=FALSE, MaxStalls=1, RealisticRoots=TRUE | TypeOK NoEarlyExit DoneOnExit AllExitedAllDone | fail(old) | FAIL NoEarlyExit | 94,635 | 24 | 1 | 2 |
| `TerminationFlags_stalebit` | Workers=3, NRoots=3, MaxSusp=3, AllowStall=FALSE, MaxStalls=1, RealisticRoots=TRUE | TypeOK NoStaleBit | fail(old) | FAIL NoStaleBit | 3,656 | 15 | 1 | 2 |
| `TerminationFlags_live` | Workers=3, NRoots=3, MaxSusp=3, AllowStall=FALSE, MaxStalls=1, RealisticRoots=TRUE | Termination | fail(old) | FAIL liveness | 692,580 | - | 18 | 2 |
| `TerminationFlags_stall` | Workers=3, NRoots=3, MaxSusp=3, AllowStall=TRUE, MaxStalls=1, RealisticRoots=TRUE | Termination | fail(old) | FAIL liveness | 923,565 | - | 23 | 2 |
| `TerminationFlags_stall_only` | Workers=3, NRoots=1, MaxSusp=0, AllowStall=TRUE, MaxStalls=1, RealisticRoots=TRUE | TypeOK NoEarlyExit DoneOnExit AllExitedAllDone NoStaleBit Termination | fail(old) | FAIL liveness | 7,086 | - | 1 | 2 |
| `TerminationFlags_nostall_small` | Workers=3, NRoots=1, MaxSusp=0, AllowStall=FALSE, MaxStalls=1, RealisticRoots=TRUE | TypeOK NoEarlyExit DoneOnExit AllExitedAllDone NoStaleBit Termination | pass | PASS | 4,797 | 34 | 1 | 2 |
| `TerminationFlags_alldone` | Workers=3, NRoots=3, MaxSusp=3, AllowStall=FALSE, MaxStalls=1, RealisticRoots=TRUE | TypeOK AllExitedAllDone | pass | PASS | 692,580 | 58 | 4 | 2 |
| `TerminationFlags_overapprox` | Workers=3, NRoots=3, MaxSusp=3, AllowStall=FALSE, MaxStalls=1, RealisticRoots=FALSE | TypeOK NoEarlyExit DoneOnExit AllExitedAllDone | fail(old) | FAIL NoEarlyExit | 40,980 | 20 | 1 | 2 |
| `Suspension` | Workers=2, Searches=2, Cells=2, SCCs={{c1, c2}}, Cap=2, MaxLives=2, MaxTransfers=1, LazyList=TRUE, NULL=NULL | TypeOK NoDoubleResume NoTwoRunners OwnedUnblocked FreeIdle NoLostWakeup | pass | PASS | 4,520,904 | 61 | 30 | 2 |
| `Suspension_b9d4` | Workers=2, Searches=2, Cells=2, SCCs={{c1, c2}}, Cap=2, MaxLives=2, MaxTransfers=1, LazyList=FALSE, NULL=NULL | TypeOK NoDoubleResume NoTwoRunners OwnedUnblocked FreeIdle NoLostWakeup | pass | PASS | 4,015,988 | 55 | 26 | 2 |
| `Suspension_sep` | Workers=2, Searches=2, Cells=2, SCCs={{c1}, {c2}}, Cap=2, MaxLives=2, MaxTransfers=1, LazyList=TRUE, NULL=NULL | TypeOK NoDoubleResume NoTwoRunners OwnedUnblocked FreeIdle NoLostWakeup | pass | PASS | 23,632,539 | 62 | 195 | 2 |
| `Suspension_big` | Workers=2, Searches=2, Cells=2, SCCs={{c1, c2}}, Cap=3, MaxLives=2, MaxTransfers=2, LazyList=TRUE, NULL=NULL | TypeOK NoDoubleResume NoTwoRunners OwnedUnblocked FreeIdle NoLostWakeup | pass | PASS | 25,956,620 | 77 | 159 | 2 |
| `Suspension_live` | Workers=2, Searches=2, Cells=2, SCCs={{c1, c2}}, Cap=2, MaxLives=1, MaxTransfers=1, LazyList=TRUE, NULL=NULL | TypeOK NoDoubleResume NoLostWakeup EventuallyResumed | pass | PASS | 537,362 | 57 | 67 | 2 |
| `Suspension_mut_norecheck` | Workers=2, Searches=2, Cells=2, SCCs={{c1, c2}}, Cap=2, MaxLives=2, MaxTransfers=1, LazyList=TRUE, **NoRecheck**, NULL=NULL | TypeOK NoDoubleResume NoTwoRunners OwnedUnblocked FreeIdle NoLostWakeup | fail(mutation) | FAIL NoLostWakeup | 7,712 | 14 | ? | 18 |
| `Suspension_mut_blind` | Workers=2, Searches=2, Cells=2, SCCs={{c1, c2}}, Cap=2, MaxLives=2, MaxTransfers=1, LazyList=TRUE, **BlindResume**, NULL=NULL | TypeOK NoDoubleResume NoTwoRunners OwnedUnblocked FreeIdle NoLostWakeup | fail(mutation) | FAIL NoDoubleResume | 12,293 | 15 | ? | 18 |
| `Suspension_mut_checkthenstore` | Workers=2, Searches=2, Cells=2, SCCs={{c1, c2}}, Cap=2, MaxLives=2, MaxTransfers=1, LazyList=TRUE, **CheckThenStore**, NULL=NULL | TypeOK NoDoubleResume NoTwoRunners OwnedUnblocked FreeIdle NoLostWakeup | fail(mutation) | FAIL NoDoubleResume | 49,202 | 18 | ? | 18 |
| `Suspension_mut_relaxed` | Workers=2, Searches=2, Cells=2, SCCs={{c1, c2}}, Cap=2, MaxLives=2, MaxTransfers=1, LazyList=TRUE, **RelaxedEntry**, NULL=NULL | TypeOK NoDoubleResume NoTwoRunners OwnedUnblocked FreeIdle NoLostWakeup | fail(mutation) | FAIL NoLostWakeup | 61,068 | 18 | ? | 18 |
| `Suspension_cover_W_StaleEntryCas` | Workers=2, Searches=2, Cells=2, SCCs={{c1, c2}}, Cap=2, MaxLives=2, MaxTransfers=1, LazyList=TRUE, NULL=NULL | W_StaleEntryCas | fail(witness) | FAIL W_StaleEntryCas | 9,425 | 14 | ? | 18 |
| `Suspension_cover_W_TwoEntriesOneSuspension` | Workers=2, Searches=2, Cells=2, SCCs={{c1, c2}}, Cap=2, MaxLives=2, MaxTransfers=1, LazyList=TRUE, NULL=NULL | W_TwoEntriesOneSuspension | fail(witness) | FAIL W_TwoEntriesOneSuspension | 22,990 | 17 | ? | 18 |
| `Suspension_cover_W_CasRace` | Workers=2, Searches=2, Cells=2, SCCs={{c1, c2}}, Cap=2, MaxLives=2, MaxTransfers=1, LazyList=TRUE, NULL=NULL | W_CasRace | fail(witness) | FAIL W_CasRace | 24,298 | 17 | ? | 18 |
| `Suspension_cover_W_NullListRace` | Workers=2, Searches=2, Cells=2, SCCs={{c1, c2}}, Cap=2, MaxLives=2, MaxTransfers=1, LazyList=TRUE, NULL=NULL | W_NullListRace | fail(witness) | FAIL W_NullListRace | 650 | 9 | ? | 18 |
| `Suspension_cover_W_TransferBeforeRecheck` | Workers=2, Searches=2, Cells=2, SCCs={{c1, c2}}, Cap=2, MaxLives=2, MaxTransfers=1, LazyList=TRUE, NULL=NULL | W_TransferBeforeRecheck | fail(witness) | FAIL W_TransferBeforeRecheck | 3,790 | 12 | ? | 18 |
| `CycleDetection_n2` | N=2, MaxSusp=3, CellsPer=2, B9d4Walk=FALSE | NoDoubleRun OneResolver MembersFrozen ResolverExclusive NoStuckCycle AllFinish | pass | PASS | 42,104 | 83 | ? | 8 |
| `CycleDetection_n3c1` | N=3, MaxSusp=3, CellsPer=1, B9d4Walk=FALSE | NoDoubleRun OneResolver MembersFrozen ResolverExclusive NoStuckCycle AllFinish | pass | PASS | 695,297 | 97 | ? | 2 |
| `CycleDetection_n3_full` (long) | N=3, MaxSusp=3, CellsPer=2, B9d4Walk=FALSE | NoDoubleRun OneResolver MembersFrozen ResolverExclusive NoStuckCycle | pass | PASS | 30,409,862 | 113 | 92 | 18 |
| `CycleDetection_n2_b9d4` | N=2, MaxSusp=3, CellsPer=2, B9d4Walk=TRUE | NoDoubleRun OneResolver MembersFrozen ResolverExclusive NoStuckCycle AllFinish | pass | PASS | 49,289 | 89 | 2 | 2 |
| `CycleDetection_n3c1_b9d4` | N=3, MaxSusp=3, CellsPer=1, B9d4Walk=TRUE | NoDoubleRun OneResolver MembersFrozen ResolverExclusive NoStuckCycle AllFinish | pass | PASS | 888,076 | 106 | 43 | 2 |
| `CycleDetection_mut_noconsensus` | N=2, MaxSusp=3, **NoConsensus**, CellsPer=2, B9d4Walk=FALSE | NoDoubleRun OneResolver MembersFrozen ResolverExclusive NoStuckCycle | fail(mutation) | FAIL OneResolver | 12,764 | 38 | ? | 8 |
| `CycleDetection_mut_walkfirst` | N=2, MaxSusp=3, **WalkBeforePublish**, CellsPer=2, B9d4Walk=FALSE | NoDoubleRun OneResolver MembersFrozen ResolverExclusive NoStuckCycle | fail(mutation) | FAIL NoStuckCycle | 1,984 | 19 | ? | 8 |
| `CycleDetection_mut_nosecondpass` | N=3, MaxSusp=3, **NoSecondPass**, CellsPer=2, B9d4Walk=FALSE | NoDoubleRun OneResolver MembersFrozen ResolverExclusive NoStuckCycle | fail(mutation) | FAIL MembersFrozen | 1,002,949 | 28 | ? | 8 |
| `CycleDetection_n2_cover_W_Resolves` | N=2, MaxSusp=3, CellsPer=2, B9d4Walk=FALSE | W_Resolves | fail(witness) | FAIL W_Resolves | 4,091 | 23 | ? | 8 |
| `CycleDetection_n2_cover_W_ConcurrentDetect` | N=2, MaxSusp=3, CellsPer=2, B9d4Walk=FALSE | W_ConcurrentDetect | fail(witness) | FAIL W_ConcurrentDetect | 11,386 | 36 | ? | 8 |
| `CycleDetection_cover_W_Resolves` | N=3, MaxSusp=3, CellsPer=2, B9d4Walk=FALSE | W_Resolves | fail(witness) | FAIL W_Resolves | 410,443 | 23 | ? | 8 |
| `CycleDetection_cover_W_ConcurrentDetect` | N=3, MaxSusp=3, CellsPer=2, B9d4Walk=FALSE | W_ConcurrentDetect | fail(witness) | FAIL W_ConcurrentDetect | 2,883,635 | 35 | ? | 8 |
| `CycleDetection_cover_W_OddAge` | N=3, MaxSusp=3, CellsPer=2, B9d4Walk=FALSE | W_OddAge | fail(witness) | FAIL W_OddAge | 745,001 | 26 | ? | 8 |
| `CycleDetection_cover_W_BlockedOnResolver` | N=3, MaxSusp=3, CellsPer=2, B9d4Walk=FALSE | W_BlockedOnResolver | fail(witness) | FAIL W_BlockedOnResolver | 841,277 | 26 | ? | 8 |

**Long runs** completed earlier with 18 workers whose output files were later overwritten by the
short configuration of the same name. Re-run them with `LONG=1 ./run.sh`.

| Config | Result | Distinct states | Depth | Time |
| --- | --- | ---: | ---: | ---: |
| `Termination_stall_big_live` | PASS | 10,919,023 | 63 | 2,237 s (18 workers; mostly the single-threaded liveness phase) |
| `TerminationBlocks_c1_live` | PASS | 5,612,890 | 58 | 1,469 s (18 workers) |

`CycleDetection_n3_full` (30,409,862 states, PASS) is in the table above; its output is kept as
`results/CycleDetection_n3_full.out`. It ran before the `CellsPer` and `B9d4Walk` constants were
added, with the semantics they now have at `CellsPer = 2, B9d4Walk = FALSE`.

## Files

| File | What it is |
| --- | --- |
| `Termination.tla` | Termination at **b9d4ba4**: the `activeSearches` counter with single-root claims |
| `TerminationBlocks.tla` | Termination at **2a280d9**: the counter with per-worker root blocks. This is the shipped version |
| `TerminationFlags.tla` | Termination at **8a9f1a9**: one bit per worker in `flags` |
| `Suspension.tla` | suspend/complete hand-shake: blocked list, `cellBlockedOn` CAS, stale entries. 2a280d9, or b9d4ba4 with `LazyList = FALSE` |
| `CycleDetection.tla` | 2a280d9 `suspend()` cycle walk: two passes, Brent checkpoint, odd-age consensus CAS, and transfer |
| `*.cfg` | one TLC configuration per model and constant set; the first line of each says what it is |
| `run.sh` | re-runs every configuration, one TLC process at a time with `-workers 2`; `LONG=1` adds the long runs |
| `scripts/trace_summary.py` | condenses a TLC counterexample to the variables each step changed |
| `scripts/results_table.py`, `scripts/build_readme.py` | regenerate the results table and this README (`python3 scripts/build_readme.py`) from `docs/*.md` and `results/` |
| `docs/*.md` | the sources of this README |
| `results/<cfg>.out` | full TLC output of the last run; counterexamples are in full here |
| `results/<cfg>.trace.txt` | condensed counterexample for every failing configuration |
| `results/summary.txt` | one line per configuration from the last `run.sh` |
| `tools/tla2tools.jar` | TLC |

Exact commands: `./run.sh`, or for a single configuration:

```
java -XX:+UseParallelGC -cp tools/tla2tools.jar tlc2.TLC -workers 2 -deadlock \
     -config <name>.cfg -metadir states/<name> <Module>.tla
```

## Model 1a: `Termination.tla`, the counter at b9d4ba4

**Code mapped** (`Tarjan Code/` at b9d4ba4):

| Action | Code |
| --- | --- |
| `GetPendingOk` / `GetPendingNone` | `multiThreadedTarjan.cpp:71` `pending.get()` |
| `IsEmpty` | `:75` `cellQueue.isEmpty()` (`stealingQueue.hpp:50` `index.load() >= ttlCells`) |
| `Inc` | `:79` `activeSearches.fetch_add(1)` |
| `NextRoot` | `:81` `cellQueue.next()`: one loop iteration of `stealingQueue.cpp:22/35` `index++` and `:32` `isNew` |
| `Claim` | `:94` `getReference` and `claimOrFail` (CAS) |
| `Dec` | `:104` `activeSearches.fetch_sub(1)` |
| `LoadActive` | `:110` `activeSearches.load() == 0`, exit |
| `Sleep` / `Stall` | `:113` `sleep_for(1ms)` |
| `Finish` / `Suspend` / `ResumeBlocked` | `worker.cpp:34` `execute()`, abstracted (see A3) |
| `RsInc` | `multiThreadedTarjan.hpp:85` `activeSearches.fetch_add(k)` |
| `RsEnq` | `:86` `pending.addPending`, one enqueue per step |
| `Stop` | `worker.cpp:35` `searchStopped()`, i.e. `multiThreadedTarjan.hpp:65` `fetch_sub(1)` |

**Abstractions.**
- A1. A search is identified by the root index that started it. Search-object recycling is irrelevant to the count and is not modeled.
- A2. Whether `next()` finds a root NEW, and whether `claimOrFail` wins, are free choices. This over-approximates the real code, which makes the PASS stronger.
- A3. `execute()` becomes a few atomic moves:
  - finish, only when no search is suspended on this one;
  - suspend on another live search;
  - resolve a blocking cycle atomically: the suspender stays running, and every other member becomes blocked on the suspender or done;
  - resume a non-empty subset of the searches suspended on it, as two steps: `+=k`, then one enqueue per search.
- A4. Pending is a set, so any dequeue order is allowed. `SpuriousEmpty = TRUE` lets `pending.get()` return null on a non-empty queue. moodycamel v1.0.4 documents this for `try_dequeue`: "the queue is likely but not guaranteed to be empty".
- A5. `MaxSusp` bounds the total number of suspensions.
- A6. An idle worker may stall forever, but only inside `sleep_for`. At most `MaxStalls` workers stall.

**Conditional result.** The termination models assume the other two protocols are correct.
- `Finish` requires that no search is waiting on the finishing one, which assumes no lost wakeup (Model 3).
- A closed cycle is always resolved, which assumes Model 4.

`DoneOnExit`, `AllExitedAllDone` and liveness depend on both. The composition is informal (assume-guarantee by hand) and is not machine-checked.

**Fairness.** Weak fairness on every worker's steps. Liveness additionally needs strong fairness on a *successful* dequeue (`SF_vars(GetPendingOk(w))`): with `SpuriousEmpty`, a worker could otherwise miss a non-empty queue forever.

**Properties.**
- `CountInv`: `activeSearches` = claiming workers + workers executing or about to call `searchStopped` + counted-but-not-enqueued resumptions + |Pending|.
- `NoEarlyExit`: once any worker exits, the roots are exhausted, nobody is claiming a root, and no search is running, pending or being resumed.
- `DoneOnExit` / `AllExitedAllDone`: on exit, every started search is done.
- `NoSuspendedCycle`: sanity check of A3.
- `Termination`: every non-stalled worker eventually returns.

**Mutations** check that the properties catch real bugs.
- `IncBeforeEnqueue = FALSE` (addPending before `+=k`): `NoEarlyExit` is violated.
- `IncBeforeTake = FALSE` (`next()` before `+=1`): `NoEarlyExit` is violated.

## Model 1b: `TerminationBlocks.tla`, the counter with root blocks at 2a280d9 (shipped)

**Code mapped** (`src/parallel/` at 2a280d9):

| Action | Code |
| --- | --- |
| `GetPendingOk` / `GetPendingNone` | `parallel_tarjan.cpp:99` `pending.get()`; the local `holdsRoots()` test at `:103` is folded in |
| `Scan` | `:110` `nextLocalRoot()`: one iteration of `worker.hpp:86-87` `cell(next++)` and `isNew()` |
| `Claim` | `:111` `claimOrFail` and the local `holdsRoots()` at `:115` |
| `Cnt` | `:116` `fetch_add(1)` |
| `Rel` | `:123-124` `fetch_sub(1)`, block used up |
| `IsEmpty` | `:128` (`root_queue.hpp:62`) |
| `Inc` | `:131` `fetch_add(1)` before the block |
| `NextChunk` | `:132` (`root_queue.hpp:55` `index.fetch_add(chunkSize)`) |
| `Dec` | `:133` `fetch_sub(1)`, no block |
| `LoadActive` | `:139` |
| `Sleep` / `Stall` | `:144` |
| `RsInc` / `RsEnq` | `parallel_tarjan.hpp:92-93` |
| `Stop` | `worker.cpp:25` (`parallel_tarjan.hpp:77`) |

`claimOrFail` is `status == NEW && CAS(NEW -> search)`. The load can only make the CAS fail early,
so the pair is a single step that succeeds iff the vertex is still NEW at that instant.

**Abstractions.**
- Vertices `1..NV` are the roots. `vstat[v]` is NEW until a search claims `v`, either as a root or through its DFS (`DfsClaim`, any time while executing). A root claim therefore fails only if some search really took the vertex.
- A ghost `bc[w]` records whether `w`'s block is counted, and a ghost `examined` records the vertices `nextLocalRoot` has looked at.
- `execute()` is abstracted as in Model 1a, with the same conditional result.
- The `aborted` exception path is not modeled.

**Properties.**
- `CountInv`: count = counted blocks + executing searches + counted-not-enqueued resumptions + |Pending|.
- `BlockInv`: a held block is counted.
- `NoEarlyExit`: no worker exits while a search is running, pending or being resumed, while a root is being claimed, or while a block is held with unexamined vertices; and every vertex has been handed out.
- `DoneOnExit` / `AllExitedAllDone`: every vertex has been examined or claimed, every vertex is claimed, and every search is done.
- `Termination`: liveness, with the same fairness as Model 1a. Stalls are allowed only in `sleep_for`, where the worker holds nothing.

**Mutation.** `CountBlockFirst = FALSE` (count after `nextChunk`): `NoEarlyExit` is violated.

## Model 2: `TerminationFlags.tla`, the old flag protocol at 8a9f1a9

**Code mapped** (`Tarjan Code/multiThreadedTarjan.cpp` at 8a9f1a9):

| Action | Code |
| --- | --- |
| `Clear` | `:65` `flags.store(0)`, at the entry of every `getSearch` |
| `GetPending` | `:73` |
| `NextRoot` | `:78` |
| `Claim` | `:92` |
| `SetBit` | `:105-109`: `flags \|= mask` when `updateFlagsASAP`, otherwise the `flags.load() & mask` of the short-circuit update. Each case is one atomic access |
| `Check` | `:111` `flags.load() == ALL_FLAGS_SET` |
| `Sleep` / `Stall` | `:114` |

`execute()` is abstracted exactly as in Model 1a. There is no `searchStopped`.

`RealisticRoots = TRUE` makes a root skippable (not NEW) or claim-losable only once some *other*
search has started, because only a running search's DFS can take a root vertex. All old-protocol
deliverables use it. `TerminationFlags_overapprox.cfg` (FALSE) is kept only for comparison: its
shortest trace loses a root claim before any search exists, which the real code cannot do.

`NoStaleBit` is a diagnostic invariant: a worker executes a search while its bit is set.

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

## Counterexamples in the OLD flag protocol (8a9f1a9)

The full TLC traces are in `results/TerminationFlags_*.out`; condensed versions are in
`results/TerminationFlags_*.trace.txt`. Workers are w1..w3, and searches are named by their root.

### (a) Safety: early exit with a stale bit (`TerminationFlags_safety`, `NoEarlyExit` violated, 24 states)

1. w1 enters `getSearch`, clears `flags`, claims root 1 and runs search 1 (states 2-5); from then on root 2's vertex may already belong to search 1. w1 finishes (6), re-enters `getSearch`, clears (7), and takes root 2 from the queue (8-9).
2. w2 enters `getSearch` and clears `flags` (10). w1's `claimOrFail` on root 2 fails, because search 1 owns vertex 2 (11). w3 enters and clears (12).
3. w1 runs `flags |= mask` (13): `flags = {w1}`. The clears by w2 and w3 landed between w1's empty checks and its bit store. w1 sees "not all set" and sleeps (14-15).
4. w1 wakes, finds Pending empty, and takes root 3 (16-17). **Its bit is still set**, although w1 is about to start work.
5. w2 and w3 find Pending empty and the queue exhausted, and set their bits (18-23): `flags = {w1, w2, w3}`.
6. w2 reads all bits set and **exits** (24) while w1 holds root 3 and is about to claim and run it.

This is the mechanism audit §8 describes: the bit is set after the empty checks rather than
atomically with them. The work w1 takes is a new root, not a Pending search, which is incidental.
The trace is realizable: with `RealisticRoots = TRUE`, root 2 can lose its claim only because
search 1 had started.

A simpler route exists too, though the audit does not list it. A worker whose root claim fails sets
its bit at once, with no exhaustion check. On its next iteration it can claim another root with the
bit still set. `TerminationFlags_live` reaches this route (see (c)).

**Consequence.** `TerminationFlags_alldone` checks `AllExitedAllDone` alone, and it **passes**. In
this abstraction the early exit never ends the run with unfinished searches. The result is lost
parallelism and the hang in (c): once a worker has exited, the next clear erases its bit for good.

### (b) Liveness: one stalled idle worker hangs everybody (`TerminationFlags_stall_only`, 30 states, lasso back to state 21)

The config has 1 root, no suspensions and `MaxStalls = 1`. Neither stale-bit route can occur here,
and `NoStaleBit` and `NoEarlyExit` hold in the same run. The failure is therefore caused by the stall
alone.

1. w1 claims root 1, runs search 1 and finishes it (states 2-11).
2. w3 sees an empty queue and sets its bit (13): `flags = {w3}`. It sees "not all set" and enters `sleep_for` (14).
3. w1 re-enters `getSearch` and clears `flags` (15).
4. **w3 stalls inside `sleep_for` forever** (16).
5. w1 and w2 set their bits (`flags = {w1, w2}`) and poll forever; w3's bit is never set again (17-30, back to 21).

Every search is done, yet nobody exits. This is the hang the audit measured: one worker sat in
`sleep_for(1ms)` while 62 others polled. `TerminationFlags_stall` (3 roots, 3 suspensions, 1 stall)
fails Termination with a trace of the same shape. `TerminationFlags_nostall_small` has the same tiny
constants without stalls, and it passes.

### (c) Liveness without any stall (`TerminationFlags_live`, lasso back to state 43)

1. w2's claim fails and w2 sets its bit (14). It then claims root 3 with the bit still set and runs search 3 (19-22).
2. w1 and w3 set their bits, so `flags = {w1, w2, w3}`, and w1 **exits** (26) while w2 is between finishing search 3 and its next `flags.store(0)`.
3. w2's clear (27) erases w1's bit permanently. w2 and w3 loop forever.

### Comparison with the counter protocols

The new protocols pass the same properties under the same search abstraction, including any number
of workers stalled in `sleep_for`:
- `Termination.tla` (b9d4ba4)
- `TerminationBlocks.tla` (2a280d9)

## What these results do and do not establish

A PASS here means "no violation in any interleaving **within the stated bounds**". It is not a
proof for arbitrary numbers of workers, searches, cells or suspensions.

These models do **not** show:
1. **Tarjan correctness.** That the right SCCs come out is not modeled: no graph edges, ranks or the transfer's rank rule (C1).
2. **Unbounded parameters.** Every model uses at most 3 workers and a handful of searches, cells and suspensions.
3. **Large blocked lists.** `BlockedListIt` (8 or more entries) is not modeled; only `firstBuffer` is.
4. **Weaker memory orders.** The real code is checked under seq_cst only. `RelaxedEntry` is a one-directional approximation used as a mutation; no C++ memory-model checker was used.
5. **Pending-queue internals.** Pending is a set; moodycamel's internals are not modeled, only its documented spurious-empty `try_dequeue`. Liveness assumes that a repeatedly attempted dequeue of a non-empty queue eventually succeeds (strong fairness).
6. **Cell recycling.** b9d4ba4 recycled cells with generation ages; 2a280d9 does not, and only 2a280d9 is modeled. The cell-age reads and checks in b9d4ba4's walk are elided. Its extra pass-1 `getBlockingCell` re-read is modeled (`B9d4Walk`).
7. **Counter wrap-around.** Search ages are 32-bit at 2a280d9, so the ABA protection of the age CAS fails after 2^31 reuses. Ages are unbounded integers here.
8. **Exceptions and partial thread start.** The `aborted` path is not modeled.
9. **A machine-checked composition.** The termination models *assume* the hand-shake (Model 3) and cycle resolution (Model 4) work. The combination is argued by hand, not checked as one model.
