# Concurrency and modernization audit

Audit date: 22 October 2026. Original revision:
[`9a97f027e5b262d54d2fe89c7da5294e9a41dcf7`](https://github.com/azabrod1/ParallelTarjan/commit/9a97f027e5b262d54d2fe89c7da5294e9a41dcf7)
(tagged `dissertation-2017`).

> **Sections 1 to 10 describe the dissertation-era code** (`Tarjan Code/`),
> with the fixes that were applied to it. The code was then restructured
> around dense vertex identifiers, which removed the hash-table dictionaries,
> cell recycling, and reference counting that several of these findings
> concern. Section 11 describes that restructure and how it was validated.
> File paths and test names in sections 1 to 10 refer to the old layout.

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

## 11. Restructure around dense vertex identifiers

Date: 23 October 2026.

### What changed

The public API now takes an immutable compressed-sparse-row `Graph` over
vertices `0 .. n-1` and returns a value-owned `Components` (one vertex buffer
plus offsets). With dense identifiers every vertex gets its own 32-byte cell
for the whole run, allocated in one array before the workers start. That makes
the following unnecessary, and they were removed:

- the `Dictionary` interface and its seven backends, including the vendored
  libcuckoo headers and the optional TBB map (sections 3, 4 and 10);
- cell recycling, generation counts, weak references and reference counting
  (sections 2 and 5): a cell can no longer change identity, so the second
  pass of the cycle walk no longer records or rechecks cell ages;
- the per-neighbor dictionary insertion and neighbor vector when a cell is
  first explored. A cell keeps a cursor into the graph's successor array
  instead. The neighbor-choice heuristic may still take one of the next 16
  successors out of turn; a 32-bit mask in the cell records which.

The suspension handshake, the cycle walk with its consensus CAS and Brent
check, cell transfer, and the active-search count are otherwise unchanged.
These details are new:

- **Lazy blocked lists.** Each cell holds an atomic pointer to its blocked
  list, installed with a CAS on first use; the worker that installs a list owns
  it, so cells are trivially destructible. A completing worker that reads a
  null pointer resumes nobody. The installing CAS, the push and the
  completeness recheck are all sequentially consistent, so the argument in
  section 6 still applies. The list's index is now 64 bits, and its capacity
  exceeds 2^32 entries, more than the number of suspensions any one cell can
  receive. (The old 19,173,960-entry limit could be exceeded, with undefined
  behavior, by a vertex with more predecessors than that.)
- **Root blocks.** Workers take up to 256 consecutive root vertices at a time
  instead of incrementing one shared counter per vertex. A worker counts the
  block in `activeSearches` before taking it and releases that count when it has
  examined every vertex in the block. When it claims the block's last root,
  the block's count passes to the new search. So the count is still zero only
  when nothing is running, pending, or held unexamined.
- **Queue tokens.** Each worker uses its own moodycamel producer and consumer
  token. Without them every dequeue scans all producers.

Workers that throw (in practice `std::bad_alloc`) record the first exception,
make the other workers stop taking searches, and `parallelScc` rethrows after
joining every thread. A failure to start a thread is handled the same way.

### Performance

`ptarjan_bench` on an Apple M5 Max (6 "Super" and 12 "Performance" cores),
macOS 27, Apple clang 21. Medians of 5 to 7 runs on graphs of 1,000,000
vertices and about 4,000,000 edges. Each batch of runs started only while all
other processes together used under one core (sampled with `top`), and was
repeated if load appeared by the end of it.

| Graph | Sequential | 1 thread | 4 threads | 16 threads |
| --- | --- | --- | --- | --- |
| `clusters` (15,625 SCCs of 64) | 12.8 ms | 25.8 ms | 6.9 ms | 3.7 ms |
| `random` (one SCC of 960,670) | 73.1 ms | 154.9 ms | 200.6 ms | 208.1 ms |
| `dag` (every SCC a single vertex) | 16.7 ms | 29.1 ms | 20.4 ms | 29.3 ms |

On 10,000,000 vertices in clusters of 1,000 (degree 6, 62.5 million edges), the
sequential algorithm took 143 ms and the parallel one 363 ms on 1 thread,
106 ms on 4, 70 ms on 8 and 55 ms on 16.

The dissertation-era engine (commit `b9d4ba4`, with the fixes of sections 1 to
10) on the same graphs, read from files with identical vertex numbering:

| Graph | Old sequential | Old parallel, 16 threads | New sequential | New parallel, 16 threads |
| --- | --- | --- | --- | --- |
| `clusters` | 123 ms | 119 ms | 12.8 ms | 3.7 ms |
| `random` | 627 ms | 430 ms | 73.1 ms | 208 ms |
| `dag` | 40.8 ms | 111 ms | 16.7 ms | 29.3 ms |

What the measurements show:

- **The hash tables were the old bottleneck.** In profiles of the old code the
  dictionary insert was one of the two most-sampled functions. The old
  sequential algorithm, which also used a hash map, was 3 to 9 times slower than
  the new CSR version. The dissertation's speedups were measured against that
  slower baseline.
- **Three code bottlenecks limited scaling, and were removed.**
  - *One shared root counter.* Every worker incremented it for every vertex,
    and workers started searches next to each other. Before root blocks the
    restructured engine was slower on `clusters` with 16 threads than with one
    (27 ms against 25 ms); with them it suspends about 5,000 times on that
    graph at 16 threads. (The old engine suspended about 2 million times at 16
    threads on a similar graph of 16-vertex cycles.)
  - *Single-threaded setup and teardown proportional to the graph.* The first
    version of the restructure copied every successor so the heuristic could
    reorder them, and destroyed every cell to free blocked lists. At 10,000,000
    vertices and 16 threads that took 9 to 19 ms before the workers started and
    about 8 ms after they stopped, in a 75 ms run. Reading the graph in place
    and making cells trivially destructible brought those to 1.2 ms and under
    0.1 ms.
  - *Dequeues that scan every producer.* Queue tokens alone cut the 16-thread
    time by 27% on `dag` and 4% on `clusters` (medians of interleaved runs).
    Together with the previous change, 16-thread times fell 27% on `dag` and
    20% on `clusters`.
  - *Idle workers sleeping at once.* Once the setup was gone, the 1 ms sleep of
    a worker that found no work became visible: work often arrives in the
    pending queue a moment later. Medians at 16 threads:

    | Idle strategy | `clusters`, 1M | `clusters`, 10M | `dag` |
    | --- | --- | --- | --- |
    | Sleep 1 ms at once (before) | 4.9 ms | 75.9 ms | 31.5 ms |
    | Yield in a loop, never sleep | 4.0 ms | 58.6 ms | 53.5 ms |
    | Yield for 200 µs, then sleep 1 ms (adopted) | 3.7 ms | 55.3 ms | 29.3 ms |

    Spinning without end slows `dag`: idle workers then hammer the queue and
    the termination count that the working threads also use.
- **Measured, and not bottlenecks.** Root blocks of 256 were about as good as
  any size: blocks of 16 were much slower, and 4,096 or more slowed `dag`.
  Relaxing memory orders on the hot path made no difference (next point).
- **The one-thread cost is 1.7 to 2.5 times the sequential cost.** It is not
  the atomic operations. On `random`, none of these changed the one-thread time
  beyond run-to-run noise (about 4%): a plain store instead of the claim CAS, a
  relaxed completion store, skipping the blocked-list check, or a relaxed
  per-edge status load. About half is the working set: padding the sequential
  algorithm's per-vertex state from 8 to 32 bytes slowed it from 76 ms to
  128 ms on `random`, and padding the parallel cells from 32 to 64 bytes
  slowed them by 15 to 19%. The rest is the parallel engine's bookkeeping: two
  stacks of cell pointers, a second pass over each completed component, and
  per-search state.
- **What limits scaling now is the dependency structure.** Share of the
  workers' time at 16 threads, measured before the idle change above:

  | Graph | Running searches | Finding work | Sleeping, nothing to do | Outside the worker loop |
  | --- | --- | --- | --- | --- |
  | `clusters`, 10,000,000 vertices | 62% | 6% | 24% | 8% |
  | `clusters`, 1,000,000 vertices | 52% | 9% | 26% | 13% |
  | `dag` | 63% | 15% | 5% | 18% |
  | `random` | 13% | 4% | 77% | 7% |

  At 8 threads on the large clustered graph, workers sleep only 1.6% of the
  time. A search follows forward edges through a chain of clusters, and a
  cluster can be completed only after everything it reaches, so with 16 workers
  there is often nothing runnable. On `random`, every search that enters the
  giant component is merged into one (292,000 cycle resolutions at 16 threads),
  so one worker does most of the work. On `dag`, searches suspend on each other
  160,000 times at 16 threads. "Outside the worker loop" is thread start-up and
  exit and the single-threaded allocation and result merge. Beyond six threads
  the workers also run on the slower Performance cores.
- **More threads than cores is costly when searches wait on each other.** On
  `dag`, 24 and 36 threads took about 128 ms, four times the 16-thread time,
  whatever idle workers do: a descheduled worker holds up every search waiting
  on its vertices. Clustered graphs lose nothing (3.6 ms at 36 threads).

### Validation

macOS 27, Apple M5 Max, Apple clang 21:

| Configuration | Result |
| --- | --- |
| Release, all 8 CTest tests | Passed |
| AddressSanitizer + UndefinedBehaviorSanitizer, all tests | Passed |
| ThreadSanitizer, all tests including the lock-free queue | Passed, no reports |
| clang-tidy with `.clang-tidy` on library, benchmark and demo | No warnings |
| Stress harness, final revision: four concurrent processes, 16 to 128 workers, 1,500 to 100,000 vertices | 640 runs, 93,600 graphs: no hang, no wrong partition |
| Stress harness, an earlier revision with the same scheduler: four concurrent processes, 16 to 128 workers | 1,280 runs, 187,200 graphs: no hang, no wrong partition |

Every stress run checks the partition against a Kosaraju oracle under a
20- or 30-second watchdog. The earlier revision differs only in joining threads through
`std::jthread`, an unbounded neighbor lookahead, and the graph-file reader.
These counts are separate from the 960 runs in section 8. GCC builds of the restructured code are covered only by
CI; they have not been run locally.

### Model checking

`spec/` contains TLA+ models of the termination, suspension and
cycle-detection protocols, checked with TLC 2.19 (`spec/README.md` has every
configuration, bound and state count). The models were written from commit
`2a280d9`, and termination also from `b9d4ba4` and from the original flag
protocol. Later changes (queue tokens, the out-of-turn successor mask,
worker-owned blocked lists, spinning before sleeping) do not alter any
modeled atomic step.

- **Termination with root blocks:** no worker exits while a search is
  running or pending or a block is held unexamined; when every worker has
  exited, every vertex has been claimed and every search is done; every
  worker eventually exits, even with idle workers stalled forever. Checked
  with 3 workers and 3 to 4 vertices (up to 1.2 million distinct states).
- **Suspension:** no search is resumed twice or lost, including stale list
  entries, the lazily allocated list, recycled searches and transfers
  (2 workers, 2 searches, 2 cells; up to 26 million distinct states).
- **Cycle detection:** concurrent resolvers never share a member, members do
  not change during a resolution, and no blocking cycle is left unresolved
  (up to 3 searches with 2 cells each; 30 million distinct states).
- **The original flag protocol fails** three ways: the early exit of section
  8 (a 24-step trace), the hang caused by one stalled idle worker, and a hang
  without any stall, when a failed root claim leaves a worker's bit set. In
  the model, neither failure ends a run with vertices missing; they lose
  parallelism or hang.

Each protocol also has deliberately broken variants (counting a root after
taking it, enqueuing before counting, no completeness recheck, no consensus
CAS, and others), and TLC catches every one. The bounds are small and the
models check the protocols, not Tarjan's ranks; `spec/README.md` lists what
they leave out.

## Remaining work and limits

1. **Smaller hot cells.** Moving the blocked-list pointer and the successor
   cursor out of `Cell` would halve it to 16 bytes. From the padding
   measurements above, that should save roughly 15% on large graphs. It
   touches the suspension protocol's code paths, so it should be made with the
   model and stress tests at hand.
2. **Idle waiting.** Workers yield for 200 µs, then poll every 1 ms.
   C++20 `atomic::wait` on the active-search count has not been measured.
3. **Packaging.** There is no install or export rule yet; consume the library
   with `add_subdirectory` or `FetchContent`. The project has no license file.
4. **ThreadSanitizer with GCC.** CI still runs only the `tsan-core` tests under
   GCC's ThreadSanitizer because of the queue's fences (see above).

## Primary references

- [Original source and dissertation](https://github.com/azabrod1/ParallelTarjan/tree/9a97f027e5b262d54d2fe89c7da5294e9a41dcf7).
- [C++ working draft: atomic ordering](https://eel.is/c++draft/atomics.order).
- [C++ working draft: data races and happens-before](https://eel.is/c++draft/intro.races).
- [LLVM ThreadSanitizer documentation](https://clang.llvm.org/docs/ThreadSanitizer.html).
- [LLVM UndefinedBehaviorSanitizer documentation](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html).
- [ConcurrentQueue upstream semantics](https://github.com/cameron314/concurrentqueue).
- [ConcurrentQueue similar TSan report](https://github.com/cameron314/concurrentqueue/issues/146).
- [ThreadSanitizer atomic-fence limitation](https://github.com/google/sanitizers/issues/1352).
