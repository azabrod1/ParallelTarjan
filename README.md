# ParallelTarjan

A parallel version of Tarjan's algorithm for strongly connected components
(SCCs), from Alex Zabrodskiy's Oxford MSc dissertation (2017, supervised by
Gavin Lowe). Several depth-first searches run at once. A search that reaches a
vertex owned by another search suspends until that vertex is assigned to a
component, and searches that end up blocking each other in a cycle are merged by
moving vertices between their stacks. See [the dissertation](dissertation.pdf)
for the algorithm, its correctness argument, and the original measurements.

Because of how it splits the work, the parallel search cannot speed up a graph
with one giant component: every search that enters it is merged into one. For
such graphs the library also has a composed algorithm that removes the giant
component first, using breadth-first parallel phases, and then runs the parallel
search on what is left. A sequential, iterative Tarjan's algorithm is the
baseline for the benchmarks and the reference in the tests.

## Build and run

Requirements: a C++20 compiler, CMake 3.20+, and a thread library. The code has
been tested with Apple clang 21; CI builds it with GCC on Ubuntu and with Apple
clang on macOS. CMake downloads one header-only dependency,
[moodycamel::ConcurrentQueue](https://github.com/cameron314/concurrentqueue)
v1.0.4, pinned by SHA-256. To build offline, set
`FETCHCONTENT_SOURCE_DIR_CONCURRENTQUEUE` to a local copy of that release.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/ptarjan_demo 8
```

The demo finds the three components of a small graph:

```text
Strongly connected components (8 workers):
{0, 1, 2}
{3, 4}
{5}
```

## Library use

Vertices are numbered `0 .. n-1`. A `Graph` is immutable and stored in
compressed sparse row form; build it from an edge list or from adjacency lists.

```cpp
#include "ptarjan/scc.hpp"

const std::vector<ptarjan::Edge> edges{{0, 1}, {1, 0}, {1, 2}};
const ptarjan::Graph graph(3, edges);

ptarjan::Components components = ptarjan::parallelScc(graph, 8);
// or multistepScc(graph, 8) for a graph with a giant component, or sequentialScc(graph)
for (std::span<const ptarjan::Vertex> component : components) {
    // ...
}
```

- `parallelScc(graph, threads)` and `multistepScc(graph, threads)` accept any
  positive number of threads and default to the hardware concurrency. Which is
  faster depends on the graph; see Benchmarks.
- `Components` owns its result: every component is a span into one shared
  buffer, so a result costs two allocations however many components there are.
- The order of components, and of vertices within a component, is
  unspecified. `normalized()` sorts both, so two results can be compared with
  `==`; `toVectors()` copies the result into nested vectors.
- If a worker throws (in practice `std::bad_alloc`), the other workers stop and
  the call rethrows the exception.
- To use graphs whose vertex identifiers are not dense, number them
  `0 .. n-1` first; `bench/graph_sources.cpp` shows one way.

Link against the `ptarjan::ptarjan` target, for example after
`FetchContent_MakeAvailable` or `add_subdirectory`.

## Benchmarks

`ptarjan_bench` times the algorithms on a seeded synthetic graph or a graph
file, and checks every parallel result against the sequential one:

```sh
./build/ptarjan_bench --graph clusters --vertices 1000000 --threads 1,2,4,8,16 --runs 7
./build/ptarjan_bench --graph edges:soc-LiveJournal1.txt --threads 8   # SNAP edge list
./build/ptarjan_bench --algorithms multistep --graph random --threads 16
./build/ptarjan_bench --help
```

The `csp:FILE` format reads the state-space graphs used in the dissertation.

Median times at 16 threads on an 18-core Apple M5 Max (6 Super and 12
Performance cores), for graphs of about four edges per vertex:

| Graph | Sequential | Parallel Tarjan | Composed |
| --- | --- | --- | --- |
| `clusters`, 1M vertices: 15,625 components of 64 | 11.9 ms | **3.4 ms (3.4×)** | 7.8 ms (1.5×) |
| `clusters`, 10M vertices: 10,000 components of 1,000 | 133 ms | **48.6 ms (2.7×)** | 113 ms (1.2×) |
| `random`, 1M vertices: one component of 960,670 | 72.7 ms | 204 ms (0.36×) | **19.2 ms (3.8×)** |
| `dag`, 1M vertices: every component a single vertex | **15.9 ms** | 30.0 ms (0.53×) | 17.8 ms (0.89×) |

Choose by the shape of the graph:

- **Many medium-sized components** (clustered graphs): `parallelScc`. The
  searches work in separate parts of the graph.
- **One giant component** (typical of web, social and citation graphs):
  `multistepScc`. It extracts the giant component with two parallel
  breadth-first searches, which scale where depth-first search cannot.
- **Long chains of dependencies** (a deep DAG): neither beats the sequential
  algorithm here. Each search waits for the vertices ahead of it, and trimming
  can only peel the DAG from its ends.

On one thread the parallel search costs about twice as much as the sequential
one: its per-vertex state is larger, and it has bookkeeping for suspending and
merging searches. Use no more threads than free cores (the default is the
hardware thread count). With more, a descheduled worker holds up every search
waiting on it: at 24 and 36 threads parallel Tarjan takes 3.5 to 4 times as long
on `dag` as at 16. [The audit, sections 11 and 12](docs/concurrency-audit.md)
break down the costs, show where the workers' time goes, and compare the
dissertation-era code on the same graphs.

## Is it lock-free?

The coordination uses no locks. Claiming a vertex, suspending a search,
recording it in a vertex's blocked list, detecting and resolving a blocking
cycle, and the termination count all use atomic operations that finish in a
bounded number of steps. The pending-search queue is lock-free apart from
memory allocation.

The algorithm as a whole is not lock-free, as the dissertation notes
(section 1.2). Only the thread running a search can advance its depth-first
search. While there are more searches than threads, a stalled thread holds up
only the searches waiting on its vertices. Near the end of a run, the remaining
threads can only wait for it, and they poll with a 1 ms sleep. Memory
allocation can also take locks.

## How this version differs from the dissertation's code

The dissertation's implementation is tagged
[`dissertation-2017`](https://github.com/azabrod1/ParallelTarjan/tree/dissertation-2017).
Since then:

- **Dense vertex identifiers.** The original mapped arbitrary vertex
  identifiers to cells through a concurrent hash table (with seven
  interchangeable backends), and recycled cells with generation counts and
  reference counting to halve its memory use. With vertices numbered
  `0 .. n-1`, each vertex has a 32-byte cell for the whole run. The hash tables, recycling, and
  reference counting are gone, along with two contended atomic operations per
  edge.
- **Termination.** One bit per worker in a 64-bit word was replaced by a count
  of active searches. The old protocol limited runs to 64 threads and could
  hang when one idle thread was descheduled; see the audit, section 8.
- **Correctness fixes.** Generation checks, publication order, a cycle walk
  that could loop, and several data races; see the audit.
- **A composed algorithm** for graphs with a giant component (`multistepScc`):
  trimming and a forward-backward search first, then the parallel search on
  the rest.
- **Root blocks.** Workers take blocks of up to 256 root vertices instead of
  one vertex at a time. This spreads the searches across the graph and removes
  a shared counter that every worker incremented for every vertex.
- **Modern C++.** C++20, value-owned results, RAII throughout, `std::jthread`,
  and a seeded benchmark tool in place of the original driver.

[docs/concurrency-audit.md](docs/concurrency-audit.md) records each defect
found in the original code, with evidence and measurements.

## Testing

The tests compare every vertex's component with an independent Kosaraju
implementation. They cover empty graphs, self-loops, duplicate edges, long
chains and cycles, seeded random graphs, 1 to 128 threads started
simultaneously, synchronized contention, many searches suspended on one vertex,
and long runs at 63 to 128 threads on small graphs, where termination problems
show up. Every differential test also checks the composed algorithm, and its own
test covers DAGs, giant components with trees on both sides, and equal-sized
components at 1 to 64 threads. CTest applies timeouts so a hang is reported as a
failure.

```sh
# Invalid memory access and undefined behavior
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DPTARJAN_SANITIZER=address
cmake --build build-asan --parallel
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build-asan --output-on-failure

# Data races
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DPTARJAN_SANITIZER=thread
cmake --build build-tsan --parallel
TSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-tsan --output-on-failure -L tsan-core
```

GCC's ThreadSanitizer does not model the atomic fences in the lock-free queue
([upstream discussion](https://github.com/cameron314/concurrentqueue/issues/146)),
so CI runs only the `tsan-core` tests, which use a mutex-backed queue. With
Clang's ThreadSanitizer on macOS the whole suite passes without reports. (On
macOS, LeakSanitizer is unavailable, so drop `detect_leaks=1`.)

### Model checking

`spec/` holds TLA+ models of the scheduler's protocols, checked with TLC
(`spec/run.sh` downloads TLC, verifies its checksum, and reruns every
configuration). Within small bounds (2 to 3 workers, 2 to 4 vertices or
searches), TLC found no counterexample for:

- **termination**, with root blocks: no worker exits while a search is
  running, waiting to run, or held in an unexamined block. Every worker
  exits once every search is done, even if idle workers stall forever;
- **suspension**: a suspended search is resumed exactly once, never lost, even
  with stale blocked-list entries, lazily allocated lists, and transfers;
- **cycle detection**: two threads never resolve overlapping blocking cycles,
  and no blocking cycle is left unresolved.

The original termination protocol fails in the model in three ways,
including the early exit and the stalled-worker hang the audit describes; the
counterexample traces are in `spec/results/`. Deliberately broken variants of
each protocol are caught, so the passes are not vacuous.
[spec/README.md](spec/README.md) maps each model action to the code and lists
what the models leave out.

## Development

The sources are formatted with clang-format 23 (`.clang-format`) and checked
with clang-tidy (`.clang-tidy`):

```sh
clang-format -i include/ptarjan/*.hpp src/*.cpp src/parallel/* bench/* examples/*.cpp tests/*.cpp
clang-tidy -p build src/*.cpp src/parallel/*.cpp bench/*.cpp examples/*.cpp
```

Both are available from PyPI (`pip install clang-format clang-tidy`). CI checks
formatting. `-DPTARJAN_WARNINGS_AS_ERRORS=ON` turns the project's compiler
warnings into errors.

| Path | Contents |
| --- | --- |
| `include/ptarjan/` | Public API: `Graph`, `Components`, `sequentialScc`, `parallelScc` |
| `src/` | The sequential algorithm, graph and result types |
| `src/parallel/` | The parallel algorithm: cells, searches, workers, suspension, termination |
| `bench/` | `ptarjan_bench` and its graph generators and file readers |
| `tests/` | CTest suites |
| `spec/` | TLA+ models of the scheduler protocols |
| `docs/` | The concurrency audit |

Contact: alex.zabrodskiy86@gmail.com.
