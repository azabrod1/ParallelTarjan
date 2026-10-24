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

