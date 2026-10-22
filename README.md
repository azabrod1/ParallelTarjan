# ParallelTarjan

A research implementation of parallel strongly connected component (SCC)
discovery, based on Tarjan's algorithm. The repository includes the original
Oxford MSc dissertation, a sequential iterative implementation, and a parallel
implementation that resolves collisions between searches by transferring cells
between their DFS stacks.

This project was written by Alex Zabrodskiy for an MSc in Computer Science at
Oxford, supervised by Gavin Lowe. See [the dissertation](dissertation.pdf) for
the algorithm, proofs, measurements, acknowledgments, and references.

## Build and run

Requirements: a C++17 compiler, CMake 3.20 or newer, and a platform thread library.
The default build does not require TBB or download dependencies. CMake detects
whether the experimental 16-byte atomic entries need `libatomic`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/parallel_tarjan_demo 8
```

The demo finds three components in a small graph:

```text
Strongly connected components (8 workers):
{0, 1, 2}
{3, 4}
{5}
```

The demo accepts **1 to 64 worker threads**. This is also the supported range of
the current library scheduler. The 64-thread boundary is handled explicitly;
larger counts require a different termination representation.

See [examples/scc.cpp](examples/scc.cpp) for graph construction, explicit backend
selection, result ownership, and sorting the output for display.

## Library use

The existing API is preserved:

```cpp
AdjacencyListGraph<Vid> graph;
graph.bulkInsertEdges({{1, 2}, {2, 1}, {2, 3}});

SCC_Set* components = Tarjan::multiThreadedTarjan(graph, 4, Sharded_Locked);
// Inspect the components, then release both levels of owned storage.
Utility::deleteSCCs(components);
```

- Use `AdjacencyListGraph` for the supported vector-based neighbor interface.
- Keep the graph unchanged until the algorithm returns.
- SCC and vertex ordering is unspecified. Compare partitions after sorting, not
  by discovery order or component count alone.
- The legacy result type is `std::vector<std::vector<Vid>*>*`; the caller owns the
  collection and every component. The demo wraps it in an RAII deleter.
- `Tarjan::singleThreadedTarjan(graph)` uses an iterative DFS and the same result
  ownership convention.

### Dictionary backends

`Sharded_Locked` is used by the demo as a straightforward baseline. Other
research backends remain available through `DictType`. The historical API
default remains `OpenSharded`, so existing benchmark calls retain their choice.

`OpenSharded` eagerly creates 8,192 tables with 2,048 entries each. With 16-byte
entries, that is about **256 MiB of entry storage before graph work starts**.
Select a backend explicitly for small graphs. Reducing this footprint and
measuring the tradeoff belongs in a separate benchmark change.

The dissertation studies lock-free coordination, but an entire run is not
unconditionally lock-free on every platform: the dictionary choice, allocator,
queue implementation, and implementation of 16-byte atomics matter. The
open-addressed table's resize protocol also waits for admitted operations.

To build the optional oneTBB backend with an installed TBB package:

```sh
cmake -S . -B build-tbb -DPARALLEL_TARJAN_ENABLE_TBB=ON
cmake --build build-tbb --parallel
```

Requesting `TBB_Conc` without enabling it throws a descriptive exception.

## Concurrency regression tests

The regression suite checks full SCC membership against an independent,
iterative Kosaraju implementation. It includes empty and disconnected graphs,
self-loops, duplicate edges, directed bridges, a long chain, seeded random
graphs, and simultaneous starts at 32 and 64 workers.

Dedicated tests cover the generation-check window during cell reuse, blocked
list growth and reset, contended tiny-table resizing, and spin-lock publication
and exception safety. CTest applies timeouts so a hang is reported as a failure.

```sh
# Invalid access, leaks, and undefined behavior
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DPARALLEL_TARJAN_SANITIZER=address
cmake --build build-asan --parallel
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
ctest --test-dir build-asan --output-on-failure

# Data races in the core with a mutex-backed pending queue, plus the
# isolated custom primitives; use a separate build from AddressSanitizer
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug \
  -DPARALLEL_TARJAN_SANITIZER=thread
cmake --build build-tsan --parallel
TSAN_OPTIONS=halt_on_error=1 \
ctest --test-dir build-tsan --output-on-failure -L tsan-core
```

The `tsan-core` selection tests the SCC algorithm with `MutexDict` and
`LockedPendingQueue`, plus the cell reference, blocked list, spin lock, and
open-addressed dictionary regressions. Release and AddressSanitizer run the
complete suite, including the historical `LockFreePendingQueue`.

The bundled concurrent queue produces a ThreadSanitizer report during the
default-queue contention test. It uses atomic fences that GCC ThreadSanitizer
warns it does not fully model; upstream has discussed a closely matching
[report](https://github.com/cameron314/concurrentqueue/issues/146). This is an
unresolved validation limitation, not a clean ThreadSanitizer result for the
default configuration. No reports are suppressed. To investigate that path,
run the ThreadSanitizer suite without the label filter.

Sanitizer coverage is useful evidence, not a proof of deadlock freedom. See
[the concurrency audit](docs/concurrency-audit.md) for the demonstrated failures,
exact fixes, and remaining limits.

## Historical benchmark

The original `Tarjan Code/main.cpp` remains available for comparison with the
dissertation. It contains local dataset paths and is excluded from the normal
build. Enable it explicitly if you intend to adapt those paths:

```sh
cmake -S . -B build-legacy -DPARALLEL_TARJAN_BUILD_LEGACY_BENCHMARK=ON
cmake --build build-legacy --parallel
```

The older `DirectedHashGraph` implementation and historical graph generators
are not the validated public interface; they need a separate ownership/API
cleanup before reuse. The original source filenames and dissertation are kept
so the implementation remains easy to compare with the research.

## Next development steps

1. Preserve these regressions while separating the public API, scheduler,
   graph representation, benchmarks, and third-party headers.
2. Add a value-owned SCC result and an immutable compact graph representation.
3. Model-check the suspension/publication and termination protocols, including
   stalled threads and reuse; add bounded exploration of external search cycles.
4. Benchmark graph families and dictionary choices with reproducible seeds,
   peak memory, medians and tail latency, and verified SCC output.
5. Then consider C++20 worker lifetime/cancellation and event-driven wakeups,
   checking the termination proof before changing the scheduler.

Contact: alex.zabrodskiy86@gmail.com.
