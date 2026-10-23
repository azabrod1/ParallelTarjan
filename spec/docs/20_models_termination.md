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
