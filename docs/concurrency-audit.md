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

The existing flag-reset protocol was not otherwise rewritten. The review did
not establish a separate counterexample to that protocol with valid masks and
correct work publication.

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

1. **Bound traversal of external suspension cycles.** The first pass stops when
   it reaches its initiating search, an unsuspended search, a missing owner, or
   a search currently transferring. A path from P into an already formed
   A-to-B-to-A cycle can keep growing its path vectors until that cycle's
   resolver runs. Under fair scheduling this is not independently proof of
   permanent deadlock, but an oversubscribed or descheduled resolver can cause
   wasted CPU and unbounded temporary growth. A generation-aware repeated-node
   guard needs validation against the cycle-resolution proof.
2. **Prove termination against the selected queue semantics.** The bundled
   concurrent queue is not generally sequentially consistent. Its documented
   empty-observation limitations must be considered explicitly. No failure is
   attributed to `size_approx()`: the wrapper's `isDone()` method is unused.
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
6. **Benchmark before changing architecture.** `OpenSharded` preallocates about
   256 MiB of entry storage on the tested 16-byte-entry platform. Sparse/lazy
   shards, compact adjacency storage, value-owned SCCs, wakeups instead of
   millisecond polling, and pool management are useful candidates. Measure
   throughput, peak memory, and tail latency on disconnected, chain, giant-SCC,
   and clustered graphs. Keep the sequential implementation and simple locked
   backend as correctness/performance baselines.
7. **Finish repository packaging.** Separate third-party headers from project
   sources, record upstream versions and notices, choose an explicit project
   license, and replace the local-path benchmark with a seeded CLI. Optional
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
