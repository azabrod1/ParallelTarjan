# Concurrency and modernization audit

Audit date: 22 October 2026. Original revision:
[`9a97f027e5b262d54d2fe89c7da5294e9a41dcf7`](https://github.com/azabrod1/ParallelTarjan/commit/9a97f027e5b262d54d2fe89c7da5294e9a41dcf7).

## Scope and conclusion

The original source has several independent correctness problems. A 64-worker
run hangs even on an empty graph. The cell recycling implementation omits a
generation check that is explicitly present in the dissertation. A contended
small open-addressed table can fill and prevent its own resize. The alternative
spin-lock dictionary has both a C++ data race and an exception-induced lock leak.

These are separate failures with different triggering conditions. Finding them
does not identify which one caused a particular historical incident, and a
passing stress test does not prove the entire algorithm deadlock-free.

The patch retains the parallel search, suspension, and stack-transfer design.
It repairs concrete ownership/publication problems and adds a portable build,
regressions, a demo, and CI. The historical dissertation and benchmark driver
remain available for comparison.

## Findings and evidence

Original source locations below refer to the revision above, not the patched
line numbers.

| Finding | Original location | Evidence | Patch |
| --- | --- | --- | --- |
| Invalid termination mask at 63/64 workers | `multiThreadedTarjan.hpp:69`; `worker.cpp:15` | UBSan confirms signed overflow at 63 and invalid shift at 64; 64-worker empty graph times out | Unsigned 64-bit masks, explicit all-bits case at 64, reject 0 and >64 |
| Cell generation checked before acquisition only | `cell.h:340-342`; `Reference.hpp:81-105` | Forced reuse schedule returns the replacement vertex on original code | Acquire the reference, then validate generation while pinned |
| Root claim bypasses generation-safe acquisition | `multiThreadedTarjan.cpp:84-92`; `stealingQueue.cpp:32` | Concrete completion/reuse interleaving between queue check and claim | Hold a validated reference through claim and root setup |
| Full table prevents its own resize | `openAddressed.h:40-87` | Eight admitted writers into four slots: four complete, four spin indefinitely | Bound a probe to one table traversal; release admission and retry |
| Spin lock does not synchronize protected data | `ShardedSpinLock.h:30-52,69` | ThreadSanitizer reports concurrent unordered-map accesses | Acquire on successful lock, release on unlock |
| Spin lock remains held after a throwing lookup | `ShardedSpinLock.h:194-213` | Missing-key lookup followed by same-shard insertion times out | Scoped lock guards release on all exits |
| SCC output reads after permission to reuse | `worker.cpp:179-181,191-194,199-202` | Concrete last-reference/reuse interleaving | Capture the vertex before dropping the owner reference |
| Blocked-list publication is weaker than the wakeup proof requires | `blockedList.hpp:159,194`; `suspensionManager.cpp:342` | C++ memory-order analysis; no claimed native x86 reproduction | Put list publication and completion reads in the sequentially consistent order |
| Mutex queue reads outside its mutex | `pending.hpp:58,72` | Unsynchronized `deque::empty()` concurrent with mutation | Lock every queue inspection |
| Graph ownership and mutation errors | `AdjacencyListGraph.h:29-30,66-67,105,124-129,155-165` | Static inspection plus ownership/mutation regressions | Value-owned vertex storage, safe moved input, complete vertices, correct erase/shuffle ranges |
| Termination waits for every worker to check in | `multiThreadedTarjan.cpp:59-114` | Ten hangs in about 900 stress runs on the 64-bit-mask fix: one worker stalled inside its 1 ms sleep while all others polled forever | Count active searches; idle workers no longer depend on each other |
| A worker can leave the idle loop with its termination bit set | `multiThreadedTarjan.cpp:97-103` | Interleaving found by inspection; not reproduced | Removed with the flag protocol |
| Cycle detection can walk round a foreign cycle indefinitely | `suspensionManager.cpp:151-173` | Code inspection; listed as open item 1 in the first revision of this audit | Brent's cycle check returns `SUSPEND` on a repeated search |
| `OpenSharded` zeroes 256 MiB per call; shard and slot hashes share a bit | `openShardedMap.hpp:19-21,32-37` | 272 MB peak RSS for a 10-vertex graph; one constant low bit within every shard | 13-bit shard index without overlap; shards start at 16 slots |

### 1. The high-thread-count termination failure

The original scheduler computes:

```cpp
(1LL << num_threads) - 1
```

With the tested C++17 implementation, 63 workers makes the subtraction overflow
a signed `long long`; 64 workers shifts by the width of the type. Both are
undefined behavior. Independently, one 64-bit field cannot represent more than
64 distinct worker flags.

An isolated baseline harness with a correct, empty graph and a mutex dictionary
was run with 64 workers under a 10-second timeout. It timed out. UBSan also
reported the invalid shift. The repaired scheduler completes at both 63 and 64
workers and rejects 0 and 65 workers before launching threads.

The first revision of this patch kept the flag-reset protocol. A later stress
campaign found that it still hangs with valid masks; see section 8. The flags
have since been replaced, which also removes the 64-worker limit.

### 2. The missing generation postcheck

Cells are kept allocated and reused for new vertices. Weak references contain
an address and generation. Checking the generation before incrementing the
reference count is insufficient:

1. Reader A observes the expected generation for vertex 101.
2. Worker B completes the cell and drops its last reference.
3. B recycles the same address, increments its generation, and initializes it
   for vertex 202 with a positive owner reference count.
4. A increments that new count and receives a valid reference to vertex 202.

The numerical count is valid, but the identity is wrong. This is an ABA-style
lifetime error.

The dissertation explicitly includes a second generation check in **Code 4.2,
lines 22-24, pages 33-35**, and explains why reuse before the successful CAS
requires undoing the increment. The checked-in implementation omitted that
step. The patch restores it and uses acquire semantics on successful reference
acquisition so the generation initialization is observed.

`tests/cell_reference.cpp` deterministically forces this schedule. It invokes
the real `Cell::getReference`, performs real completion/recycling/reinitialization
on another thread, and uses a test-only acquisition specialization to pause at
the precise boundary. The specialization performs a real count CAS and returns
the real RAII reference; it replaces no production code in the library. The
test fails on the original cell method and passes on the patched one, including
checking that rejection releases its temporary reference.

Root creation had a second path around this protection. A queue-side
`isNew(expectedAge)` check is now followed by a generation-validated reference
that remains alive through claiming and initializing the root.

### 3. The open-addressed table can trap its active writers

The load-factor check is an admission check, not a reservation for a free slot.
Several writers can all pass it before any writer increments the recorded size.
If more writers enter than the remaining capacity, the table can become full.

In the original implementation, remaining writers probe forever while counted
in `members`. The resizer waits for `members == 0`. Thus the writers prevent the
only operation that could give them space.

A temporary scheduling hook paused eight writers immediately after successful
admission to a four-slot table, then released them together. On the original
code exactly four finished and the rest hit the timeout. With the patch all
eight finished. A natural synchronized 32-worker baseline run with small tables
also timed out; the forced schedule isolates the cause from the SCC machinery.

The fix allows a full traversal of the current table. A writer that finds no
slot drops membership and retries admission, allowing a resize. Waiters also
yield while an exclusive resizer publishes its result. This does not introduce
a global lock or redesign the resize protocol.

During ThreadSanitizer testing, the bounded-probe version still stalled in the
resize wait. Temporary phase tracking showed the owner after `resize()` returned
but before the following size store, with other workers hammering the zero-size
gate. Adding only a yield to that wait let the same 32-worker, 20-round test
complete. Scheduling or instrumentation starvation is the inferred explanation;
there is no debugger trace proving a particular ThreadSanitizer internal fault.
This observation is distinct from the full-table deadlock reproduced above.

**Scope qualification:** the normal table starts with 2,048 slots. The small
capacity constructor is what makes this failure easy to trigger at 8-64
workers. It should not be presented as a demonstrated cause in the historical
default configuration.

### 4. Spin-lock exclusion, publication, and exception safety

The original lock bit is atomic, but both acquiring and releasing it use
`memory_order_relaxed`. That alone does not establish the happens-before
relationship needed for accesses to the ordinary `unordered_map` inside the
critical section. The C++ memory model requires synchronization of the data as
well as atomic modification of the lock bit.

The same-shard regression uses eight writers and 4,096 keys, checking duplicate
inserts, cardinality, and stored values. ThreadSanitizer reports a data race in
the original map insertion. The fixed acquire/release lock passes that test.

There is also a deterministic exception path: `get(missingKey)` takes the lock,
then `at()` throws before the manual unlock. A later insertion into that shard
spins forever. Scoped guards now release the lock on normal returns and throws.

Insertion results are copied while the shard is still locked; otherwise another
insertion can rehash and invalidate the returned iterator before it is used.
The same rule is applied to the legacy `putAll` optimization. Size/enumeration
reads are guarded, and key enumeration reconstructs the original key from its
shard and stored portion.

### 5. Completion must read output before releasing ownership

Original completion paths drop the artificial owner reference and then read
`cell->vertex` to append the result. If another worker holds the final real
reference, that worker can release, recycle, and reuse the cell between those
operations. The original worker can then append another vertex's ID and race
with the write of that ID.

All three completion paths now append the vertex before permitting recycling.
This is a direct ownership-order fix. The review established the schedule by
inspection; unlike the generation check and table admission cases, no separate
forced-schedule reproduction is claimed for this exact path.

A later stress campaign of the original code on macOS (Apple M5 Max, 18 cores)
returned 8 incorrect partitions in 28,000 random graphs at 32 to 63 workers.
Each had exactly one duplicated and one missing vertex, which is what either
this path or section 2's generation window produces. The patched code returned
none in 27,600 graphs from the same campaign.

### 6. Blocked-list publication and wakeups

The intended handshake is: publish the list entry, publish the search's blocked
state, check the cell's completion state, and have the completing worker scan
the list. If the suspending worker sees completion, it handles its own wakeup;
otherwise the completing worker must find it.

The original list stores are relaxed. The small-list reader is also relaxed.
Sequentially consistent operations on the search and cell state do not
automatically make relaxed accesses to a different atomic object participate
in the same total order. This leaves a standards-level gap in the proof of
who performs the wakeup.

The patch makes both insertion paths and the small-list scanner sequentially
consistent. The large-list iterator already uses sequentially consistent
loads. This is a conservative C++ memory-model repair, not a claim that a
native x86 lost wakeup was reproduced. Its performance cost should be measured
before weakening the ordering with a separately proved fence protocol.

The reservation-before-publication case in `BlockedList` is intentional: an
iterator may miss an unfilled slot, with the completion recheck handling that
case. Reset requires quiescence of the cell lifetime, which the corrected
reference protocol supplies.

### 7. Storage lifetime and smaller fixes

Worker recycling pools are now deleted after **all** workers join. Neighbor
selection and cycle detection deliberately inspect atomics through weak
pointers, so the physical storage must outlive those inspections. This makes
that boundary explicit; it is defensive hardening rather than a separately
reproduced original shutdown crash.

Resolver election now uses `std::less<Search*>` instead of built-in comparison
between unrelated pointers, providing a portable total order. Weak references
are initialized to null and their acquisition helper uses generation checks.
The public wrapper owns its dictionary with `unique_ptr`, including exception
paths. Missing direct standard-library includes are supplied, and TBB is an
explicit optional dependency.

The graph fixes replace manually owned cached vertex arrays with a vector,
avoid deleting the caller's moved-from map, include destination-only vertices,
keep cached vertices consistent with mutation, remove incoming edges when a
vertex is removed, terminate edge removal, and use the correct shuffle end.
The CSP importer now owns its input map locally and rejects malformed vertex
prefixes rather than using an uninitialized vertex.

### 8. Termination waited for every worker to check in

The original protocol gives each worker one bit in a shared word. A worker
entering `getSearch` clears every bit. An idle worker sets its own bit one
polling round after it sees the bit cleared, and all workers exit once every
bit is set. This has two weaknesses that remain after the mask repair.

**Every worker must make progress.** A worker that is descheduled or stalled
holds up termination for all of them. On macOS 27 with an Apple M5 Max (18
cores), four concurrent stress processes, each running 48 or 63 workers on
1,500 to 3,000-vertex random graphs, hung ten times in about 900 runs of 200
graphs on the branch with only the mask repair. In each instrumented hang, 62
workers had completed about 13,800 polling iterations while the missing bit's
worker had completed two. Phase markers placed it inside
`std::this_thread::sleep_for(1ms)` for the entire 20-second watchdog interval;
three such processes stayed hung for between 3.5 and 10 minutes, until they
were stopped. A 50-line program with no code from this project, which repeatedly
starts 63 threads that each sleep for 1 ms, recorded one sleep longer than 1.2
seconds under the same load. The stall originates outside this code, but the
protocol turns one stalled idle thread into a hang of the whole run.

**A worker can leave with a stale bit.** The bit is set after the empty checks,
not atomically with them. If another worker enters `getSearch` and clears the
word between a worker's checks and its `flags |= mask`, the bit is set without a
fresh check. On its next iteration that worker can take a search while its bit
remains set. If no other worker then enters `getSearch` while it runs, the idle
workers observe every bit set and exit, and the remaining worker can never see
every bit set again. This needs the clearing worker to be delayed for about one
polling interval before its own first check, which oversubscription allows. It
was found by inspection and has not been reproduced.

The patch replaces the flags with `activeSearches`, the number of searches that
are running or waiting in `Pending`, plus workers that are claiming a root.
A search is counted before it is added to `Pending` and uncounted after its
worker stops executing it, whether it finished or suspended. Suspended searches
are not counted, since only a running search can complete the cell a suspended
search waits on. A worker exits when the root queue is exhausted and the count
is zero; at that point no search can become runnable again. Workers no longer
need to observe each other. A claim that fails while roots remain is retried
immediately rather than after a 1 ms sleep.

The same stress campaign then ran 960 runs (192,000 graphs) at 48 and 63
workers with no hang and no incorrect partition.

Blocking idle workers on a condition variable instead of polling was also
measured. It made small graphs about ten times faster, but a one-million-vertex
graph with 16 workers took 669 ms instead of 295 ms, because every resumption
then locked a mutex and woke a thread on the running search's critical path.
Polling was kept.

### 9. Cycle detection could walk round a foreign cycle

The first pass of `suspend()` follows blocking edges from the new search until
it returns to it or the path ends. If the path enters a blocking cycle that
does not contain the new search, the walk goes round that cycle, appending to
the path vectors, until the cycle's own last suspender resolves it. With a
descheduled resolver that can be a long time.

The walk now applies Brent's cycle detection: it remembers the search at each
power-of-two step and returns `SUSPEND` if it meets that search again. In a
consistent view, the path from a search that lies on a cycle returns to that
search before it repeats any other, so a repeat proves the new search is not on
the cycle. A repeat seen while the graph is changing is handled like the walk's
other early returns, which also suspend on inconsistent observations. The last
search to join a cycle sees a stable cycle and is not affected.

### 10. The default dictionary's footprint and hash split

`OpenSharded` used `SHARDS = 2 << 12` (8,192) shards with `key1 = hash & 8191`
and `key2 = hash >> 12`. The two values share hash bit 12, so within a shard
the low bit of `key2` is constant and linear probing initially used only half
of the slots. Each shard also started with 2,048 entries: 256 MiB to allocate
and zero per call. In the stress harness, a 10-vertex graph peaked at 272 MB of
resident memory. The patch uses a 13-bit shard index and starts each shard at
16 entries; the same run now peaks at 4.5 MB, and a one-million-vertex run
(including the test oracle) at 547 MB rather than 723 MB. Medians on 200,000
and 1,000,000-vertex graphs, measured together with section 8's change, are
unchanged within noise; an 8-worker run on a 1,000-vertex graph fell from
7.0 ms to 2.1 ms.

## Validation

The baseline was preserved separately before production changes. Compatibility
adjustments for baseline compilation supplied a missing `<vector>` include
and bypassed an unnecessary include that pulled in unavailable TBB headers.
They did not change the concurrency logic under test.

The nine CTest groups cover graph API regressions, partition differential tests,
thread-count boundaries, synchronized contention with each pending-queue type,
blocked-list growth, spin-lock regressions, direct dictionary contention, and
the deterministic generation-window regression. The independent SCC oracle is
iterative Kosaraju, and checks compare every vertex's component rather than
just the number of components. Seeds are fixed for reproduction.

Final local validation used GCC 13.3, C++17, and Linux:

| Configuration | Result | Elapsed time |
| --- | --- | --- |
| RelWithDebInfo, all tests | 9/9 passed | 0.90 s |
| AddressSanitizer + UndefinedBehaviorSanitizer + LeakSanitizer, all tests | 9/9 passed | 9.86 s |
| ThreadSanitizer, explicit `tsan-core` selection | 5/5 passed | 11.22 s |
| Demo with 64 workers | Expected three components | — |

Memory sanitizer validation used `detect_leaks=1` and strict halt-on-error
options. Local restricted runs could not inspect some `/proc` entries; the
final run had access to the required process metadata. Leak checking was not
suppressed. Hosted Linux/macOS CI is configured separately from this local
verification, and the elapsed times above are test timings, not benchmarks.

A second validation ran on macOS 27 (Apple M5 Max, Apple clang 21, CMake 4.4)
after the changes in sections 8 to 10:

| Configuration | Result |
| --- | --- |
| RelWithDebInfo, all tests, 20 repeats with 6 in parallel | 10/10 passed |
| AddressSanitizer + UndefinedBehaviorSanitizer, all tests, 10 parallel repeats | 10/10 passed |
| ThreadSanitizer, all tests including the default queue | 10/10 passed, no reports |
| Stress harness: 960 runs of 200 random graphs at 48 and 63 workers, four concurrent processes | No hang, no incorrect partition |

LeakSanitizer is not available on that platform. Under heavy parallel load,
ThreadSanitizer-instrumented test processes occasionally froze at 0% CPU with
only 3 of their 33 threads created. This included the open-addressed dictionary
test, which does not use the scheduler. One parallel ThreadSanitizer run of the
termination test also timed out; it was not captured, and 60 further parallel
ThreadSanitizer runs of that test passed. No such freeze occurred in
uninstrumented or AddressSanitizer builds.

### ThreadSanitizer scope and unresolved queue report

The `tsan-core` label covers the SCC core with `MutexDict` and
`LockedPendingQueue`, including 30 synchronized 32/64-worker graph rounds,
plus the isolated blocked-list, spin-lock, open-addressed dictionary, and cell
reference tests. The ordinary and memory-sanitizer suites retain the tests
using the historical `LockFreePendingQueue`.

The default queue produces a ThreadSanitizer report at the vendored
`ConcurrentQueue.h` implicit enqueue/dequeue element access during contention.
GCC also warns that ThreadSanitizer does not support `atomic_thread_fence`.
The upstream project discusses a closely matching
[report in issue 146](https://github.com/cameron314/concurrentqueue/issues/146#issuecomment-453202961)
and [fence/annotation limitations in issue 49](https://github.com/cameron314/concurrentqueue/issues/49#issuecomment-223022425).
The sanitizer maintainers also document the
[fence limitation](https://github.com/google/sanitizers/issues/1352).

Those sources make a tool limitation plausible, but they do not prove that
this exact report is false. The default configuration is **not claimed to be
ThreadSanitizer-clean**. No warnings or reports are suppressed, and running
CTest without the label filter leaves that path available for investigation.
Changing the queue or adding precise, justified sanitizer annotations should
be a separate reviewed change.

## Remaining work and limits

1. **Check the cycle-walk bound against the proof.** Section 9's repeated-search
   check is argued above but has not been model-checked.
2. **Model-check termination.** The active-search count does not rely on the
   queue observing emptiness, since a search in `Pending` is always counted,
   but the argument in section 8 has not been model-checked.
3. **Define resource-failure behavior.** Exceptions in worker threads and
   partial thread creation are not yet converted into a cooperative error
   result. Adding `jthread` alone would not solve termination if fewer workers
   than expected actually start.
4. **Replace legacy ownership and APIs.** Prefer a value-owned SCC result,
   const graph views, and explicit graph-building/finalization. The older
   `DirectedHashGraph` still has manual ownership defects and does not implement
   the vector-neighbor interface the current algorithms use. Historical
   generators and that representation are outside the validated public path.
5. **Review finite counters and capacities.** Generations and vertex IDs are
   32-bit; `BlockedList` has a finite template-dependent maximum without a
   checked overflow path. The default list capacity is 19,173,960 entries.
   These limits deserve explicit contracts before claiming general-purpose
   library support.
6. **Benchmark before changing architecture.** Compact adjacency storage,
   value-owned SCCs, wakeups instead of millisecond polling (see section 8 for
   one measured regression), and pool management are useful candidates. Measure
   throughput, peak memory, and tail latency on disconnected, chain, giant-SCC,
   and clustered graphs. Keep the sequential implementation and simple locked
   backend as correctness/performance baselines.
7. **Finish repository packaging.** The concurrent queue is now fetched at a
   pinned upstream release; the libcuckoo headers used by the `Cuckoo` backend
   are still vendored. Record upstream versions and notices, choose an explicit
   project license, and replace the local-path benchmark with a seeded CLI. Optional
   TBB and all experimental backends need their own validation matrix before
   making broad portability claims.

## Primary references

- [Original source and dissertation](https://github.com/azabrod1/ParallelTarjan/tree/9a97f027e5b262d54d2fe89c7da5294e9a41dcf7).
- [C++ working draft: atomic ordering](https://eel.is/c++draft/atomics.order).
- [C++ working draft: data races and happens-before](https://eel.is/c++draft/intro.races).
- [LLVM ThreadSanitizer documentation](https://clang.llvm.org/docs/ThreadSanitizer.html).
- [LLVM UndefinedBehaviorSanitizer documentation](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html).
- [ConcurrentQueue upstream semantics](https://github.com/cameron314/concurrentqueue).
- [ConcurrentQueue similar TSan report](https://github.com/cameron314/concurrentqueue/issues/146).
- [ThreadSanitizer atomic-fence limitation](https://github.com/google/sanitizers/issues/1352).
