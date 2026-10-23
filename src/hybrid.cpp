// The hybrid algorithm: parallel phases that remove the parts of a graph
// where depth-first search parallelizes badly, then the parallel Tarjan engine for the rest.
//
//  1. Trim: a vertex with no in-edges or no out-edges from the remaining vertices is a
//     component of its own. Removing it can expose more such vertices, so trimming runs
//     from a shared work queue until none is left. This alone finishes a DAG.
//  2. Forward-backward: the vertices reachable both forwards and backwards from a pivot
//     form the pivot's component. With the pivot of largest in-degree x out-degree, this is
//     usually the giant component of graphs that have one.
//  3. The remaining vertices go to the parallel Tarjan engine, with every removed vertex
//     marked complete beforehand, so the engine and its protocols run unchanged.
//
// Each removed set is a union of whole components, so no remaining component passes
// through a removed vertex. Phases 1 and 2 are breadth-first and synchronous: they run on
// one set of threads separated by barriers.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "concurrentqueue.h"
#include "parallel_tarjan.hpp"
#include "ptarjan/scc.hpp"

namespace ptarjan {

namespace {

constexpr std::uint8_t ALIVE = 0;
constexpr std::uint8_t REMOVED = 1;

//Hands out consecutive index blocks of [0, size) from a shared counter
class BlockCounter {
public:
    void reset(std::size_t newSize) {
        size = newSize;
        next.store(0, std::memory_order_relaxed);
    }

    bool take(std::size_t& begin, std::size_t& end, std::size_t block = BLOCK) {
        begin = next.fetch_add(block, std::memory_order_relaxed);
        if (begin >= size)
            return false;
        end = std::min(begin + block, size);
        return true;
    }

private:
    static constexpr std::size_t BLOCK = 2048;
    std::atomic<std::size_t> next{0};
    std::size_t size = 0;
};

//A barrier for the phases. std::barrier would do, but libc++ performs part of each arrival
//inside its shared library, where ThreadSanitizer cannot see the ordering it provides
class PhaseBarrier {
public:
    explicit PhaseBarrier(unsigned count) : count(count) {}

    //Waits until every thread has arrived. The last to arrive runs onLast before any
    //thread continues
    template <class OnLast>
    void arriveAndWait(OnLast onLast) {
        const std::uint32_t phase = generation.load(std::memory_order_acquire);
        if (arrived.fetch_add(1, std::memory_order_acq_rel) + 1 == count) {
            arrived.store(0, std::memory_order_relaxed);
            onLast();
            generation.store(phase + 1, std::memory_order_release);
            generation.notify_all();
            return;
        }
        //Phases are short, so spin a little before blocking
        for (int spin = 0; spin < 100; ++spin) {
            if (generation.load(std::memory_order_acquire) != phase)
                return;
            std::this_thread::yield();
        }
        generation.wait(phase, std::memory_order_acquire);
    }

private:
    const unsigned count;
    std::atomic<unsigned> arrived{0};
    std::atomic<std::uint32_t> generation{0};
};

class Hybrid {
public:
    Hybrid(const Graph& graph, unsigned threadCount)
        : graph(graph), n(graph.vertexCount()), threadCount(threadCount),
          state(std::make_unique<std::atomic<std::uint8_t>[]>(n)),
          forward(std::make_unique<std::atomic<std::uint8_t>[]>(n)),
          backward(std::make_unique<std::atomic<std::uint8_t>[]>(n)),
          inDegree(std::make_unique<std::atomic<std::uint32_t>[]>(n)),
          outDegree(std::make_unique<std::atomic<std::uint32_t>[]>(n)),
          inOffsets(n + std::size_t{1}), trimmed(threadCount), nextFrontiers(threadCount),
          giantParts(threadCount), pivotCandidates(threadCount), roots(n, threadCount),
          engine(graph, threadCount, pending, roots), barrier(threadCount) {}

    Components run() {
        //Threads wait at a start gate until all of them exist, so that a thread that fails to
        //start can never leave the others waiting at a barrier
        std::atomic<int> gate{WAIT};
        std::vector<std::thread> threads;
        threads.reserve(threadCount);
        try {
            for (unsigned id = 0; id < threadCount; ++id)
                threads.emplace_back([this, id, &gate] {
                    gate.wait(WAIT, std::memory_order_acquire);
                    if (gate.load(std::memory_order_acquire) == GO)
                        phases(id);
                });
            gate.store(GO, std::memory_order_release);
        } catch (...) {
            fail(std::current_exception());
            gate.store(CANCEL, std::memory_order_release);
        }
        gate.notify_all();
        for (std::thread& thread : threads)
            thread.join();
        if (failure)
            std::rethrow_exception(failure);

        const Components rest = engine.run();

        std::vector<Vertex> vertices;
        vertices.reserve(n);
        std::vector<std::size_t> offsets{0};
        for (const auto& part : trimmed) {
            for (const Vertex vertex : part) {
                vertices.push_back(vertex);
                offsets.push_back(vertices.size());
            }
        }
        const std::size_t giantStart = vertices.size();
        for (const auto& part : giantParts)
            vertices.insert(vertices.end(), part.begin(), part.end());
        if (vertices.size() != giantStart)
            offsets.push_back(vertices.size());
        for (const auto component : rest) {
            vertices.insert(vertices.end(), component.begin(), component.end());
            offsets.push_back(vertices.size());
        }
        return {std::move(vertices), std::move(offsets)};
    }

private:
    const Graph& graph;
    const Vertex n;
    const unsigned threadCount;

    std::unique_ptr<std::atomic<std::uint8_t>[]> state;
    std::unique_ptr<std::atomic<std::uint8_t>[]> forward;
    std::unique_ptr<std::atomic<std::uint8_t>[]> backward;
    //In- and out-degree counting only edges between remaining vertices
    std::unique_ptr<std::atomic<std::uint32_t>[]> inDegree;
    std::unique_ptr<std::atomic<std::uint32_t>[]> outDegree;

    //The reverse graph: the predecessors of v are sources[inOffsets[v] .. inOffsets[v + 1])
    std::vector<std::size_t> inOffsets;
    std::vector<Vertex> sources;

    //Building it without atomics: each thread sorts its edges into buckets of consecutive
    //target vertices, and then each bucket is built by one thread
    std::size_t bucketWidth = 1;
    std::size_t bucketCount = 1;
    std::vector<std::size_t> bucketCounts; //[thread * bucketCount + bucket], then positions
    std::vector<std::size_t> bucketStarts; //Where each bucket's edges start, plus the total
    std::vector<Vertex> scatteredTargets;  //Edges grouped by bucket; freed once the reverse
    std::vector<Vertex> scatteredSources;  //graph is built, and the sources become `sources`

    BlockCounter blocks;
    moodycamel::ConcurrentQueue<Vertex> trimQueue;
    //Vertices waiting in trimQueue plus threads holding trim work of their own; see trim()
    std::atomic<std::int64_t> trimPending{0};
    std::vector<std::vector<Vertex>> trimmed; //Per thread: vertices removed as single components

    std::vector<Vertex> frontier;
    std::vector<std::vector<Vertex>> nextFrontiers;
    //Whether a search is finished, as decided in the phase that advances the frontier on one
    //thread and in the last phase of a parallel level. Each flag is written only in its own
    //phase and read only after that phase's barrier, and a full barrier separates every
    //read from the next write. One flag for both would not do: the one-thread phase
    //directly follows the last parallel phase, so thread 0 could overwrite the flag while
    //slower threads were still reading it
    bool smallFrontierDone = false;
    bool levelDone = false;
    std::vector<std::vector<Vertex>> giantParts;
    //Per thread: its remaining vertex of largest in-degree x out-degree, if it has one
    std::vector<std::optional<std::pair<std::uint64_t, Vertex>>> pivotCandidates;
    Vertex pivot = 0;
    bool havePivot = false;

    detail::LockFreePendingQueue pending{threadCount};
    detail::VertexOrderRoots roots;
    detail::ParallelTarjan engine;

    static constexpr int WAIT = 0, GO = 1, CANCEL = 2;

    PhaseBarrier barrier;
    std::atomic<bool> aborted{false};
    bool stopped = false; //aborted as it was when the last thread reached the latest barrier
    std::mutex failureMutex;
    std::exception_ptr failure;

    void fail(std::exception_ptr error) {
        {
            const std::scoped_lock lock(failureMutex);
            if (!failure)
                failure = std::move(error);
        }
        aborted = true;
    }

    //Runs one phase on every thread, then waits for all of them. Every thread passes every
    //barrier, even after a failure, so no thread waits forever for one that gave up
    template <class Work>
    void phase(Work work) {
        if (!stopped) {
            try {
                work();
            } catch (...) {
                fail(std::current_exception());
            }
        }
        //The last thread to arrive takes the snapshot, so every thread sees the same answer to
        //"stop?" until the next barrier
        barrier.arriveAndWait([this] { stopped = aborted.load(); });
    }

    //Thread `id` of `threadCount` owns the vertices [first, last) for whole-array passes
    std::pair<std::size_t, std::size_t> range(unsigned id) const {
        return {std::size_t{n} * id / threadCount, std::size_t{n} * (id + 1) / threadCount};
    }

    //Thread `id`'s share of the source vertices, balanced by edge count: [first, last)
    std::pair<std::size_t, std::size_t> edgeRange(unsigned id,
                                                  std::span<const std::size_t> offsets) const {
        auto start = [&](unsigned thread) -> std::size_t {
            if (thread == threadCount)
                return n;
            const std::size_t edge = graph.edgeCount() * thread / threadCount;
            return static_cast<std::size_t>(
                std::lower_bound(offsets.begin(), offsets.end() - 1, edge) - offsets.begin());
        };
        return {start(id), start(id + 1)};
    }

    bool tryRemove(Vertex vertex) {
        std::uint8_t expected = ALIVE;
        return state[vertex].compare_exchange_strong(expected, REMOVED, std::memory_order_relaxed);
    }

    void phases(unsigned id) {
        const auto offsets = graph.offsets();
        const auto targets = graph.targets();
        const auto [first, last] = range(id);
        //In-degrees fit the 32-bit counters unless there are 2^32 edges or more
        const bool trimming = graph.edgeCount() < std::numeric_limits<std::uint32_t>::max();

        // Reverse graph. Atomic counters per target vertex were the slowest part of the whole
        // algorithm: every edge bounced a cache line between cores.
        const auto [edgeFirst, edgeLast] = edgeRange(id, offsets);
        phase([&] {
            if (id == 0) {
                bucketCount = std::clamp<std::size_t>(n / 2048, 1, 4096);
                bucketWidth =
                    std::max<std::size_t>((std::size_t{n} + bucketCount - 1) / bucketCount, 1);
                bucketCount = (std::size_t{n} + bucketWidth - 1) / bucketWidth;
                bucketCounts.assign(std::size_t{threadCount} * bucketCount, 0);
                bucketStarts.assign(bucketCount + 1, 0);
                scatteredTargets.resize(graph.edgeCount());
                scatteredSources.resize(graph.edgeCount());
            }
        });
        phase([&] {
            std::size_t* const counts = bucketCounts.data() + std::size_t{id} * bucketCount;
            for (std::size_t edge = offsets[edgeFirst]; edge < offsets[edgeLast]; ++edge)
                ++counts[targets[edge] / bucketWidth];
        });
        phase([&] {
            if (id != 0)
                return;
            //Bucket by bucket, thread by thread: where each thread's edges of a bucket go
            std::size_t position = 0;
            for (std::size_t bucket = 0; bucket < bucketCount; ++bucket) {
                bucketStarts[bucket] = position;
                for (unsigned thread = 0; thread < threadCount; ++thread) {
                    std::size_t& slot = bucketCounts[std::size_t{thread} * bucketCount + bucket];
                    const std::size_t count = slot;
                    slot = position;
                    position += count;
                }
            }
            bucketStarts[bucketCount] = position;
            blocks.reset(bucketCount);
        });
        phase([&] {
            std::size_t* const next = bucketCounts.data() + std::size_t{id} * bucketCount;
            for (std::size_t source = edgeFirst; source < edgeLast; ++source)
                for (std::size_t edge = offsets[source]; edge < offsets[source + 1]; ++edge) {
                    const Vertex target = targets[edge];
                    const std::size_t at = next[target / bucketWidth]++;
                    scatteredTargets[at] = target;
                    scatteredSources[at] = static_cast<Vertex>(source);
                }
        });
        phase([&] {
            //Each bucket's edges already sit where its vertices' reverse edges belong, so
            //sorting them by target within the bucket turns scatteredSources into the reverse
            //graph's sources without a second edge-sized array
            std::vector<std::uint32_t> local;
            std::vector<Vertex> sorted;
            std::size_t begin, end;
            while (blocks.take(begin, end, 1)) {
                const std::size_t bucket = begin;
                const std::size_t base = bucket * bucketWidth;
                const std::size_t width = std::min<std::size_t>(bucketWidth, n - base);
                local.assign(width, 0);
                for (std::size_t at = bucketStarts[bucket]; at < bucketStarts[bucket + 1]; ++at)
                    ++local[scatteredTargets[at] - base];
                std::size_t position = bucketStarts[bucket];
                for (std::size_t i = 0; i < width; ++i) {
                    inOffsets[base + i] = position;
                    position += local[i];
                    local[i] = 0;
                }
                const std::size_t bucketFirst = bucketStarts[bucket];
                const std::size_t bucketLast = bucketStarts[bucket + 1];
                sorted.resize(bucketLast - bucketFirst);
                for (std::size_t at = bucketFirst; at < bucketLast; ++at) {
                    const std::size_t target = scatteredTargets[at];
                    sorted[inOffsets[target] - bucketFirst + local[target - base]++] =
                        scatteredSources[at];
                }
                std::copy(sorted.begin(), sorted.end(), scatteredSources.begin() + bucketFirst);
            }
            if (id == 0)
                inOffsets[n] = graph.edgeCount();
        });
        phase([&] {
            if (id != 0)
                return;
            sources = std::move(scatteredSources);
            std::vector<Vertex>().swap(scatteredTargets);
        });

        // Trim, seeded by every vertex that already has no in- or out-edges.
        if (trimming) {
            phase([&] {
                std::vector<Vertex> seeds;
                for (std::size_t v = first; v < last; ++v) {
                    const auto in = static_cast<std::uint32_t>(inOffsets[v + 1] - inOffsets[v]);
                    const auto out = static_cast<std::uint32_t>(offsets[v + 1] - offsets[v]);
                    inDegree[v].store(in, std::memory_order_relaxed);
                    outDegree[v].store(out, std::memory_order_relaxed);
                    if ((in == 0 || out == 0) && tryRemove(static_cast<Vertex>(v)))
                        seeds.push_back(static_cast<Vertex>(v));
                }
                //Counted before they become visible, so trimPending never reads 0 too early
                trimPending.fetch_add(static_cast<std::int64_t>(seeds.size()));
                if (!trimQueue.enqueue_bulk(seeds.begin(), seeds.size()))
                    throw std::bad_alloc();
            });
            phase([&] { trim(id); });
        }

        // Forward-backward from the remaining vertex of largest in-degree x out-degree.
        phase([&] {
            std::optional<std::pair<std::uint64_t, Vertex>> best;
            for (std::size_t v = first; v < last; ++v) {
                if (state[v].load(std::memory_order_relaxed) != ALIVE)
                    continue;
                const std::uint64_t score =
                    trimming ? std::uint64_t{inDegree[v].load(std::memory_order_relaxed)} *
                                   outDegree[v].load(std::memory_order_relaxed)
                             : 1;
                if (!best || score > best->first)
                    best = std::pair{score, static_cast<Vertex>(v)};
            }
            pivotCandidates[id] = best;
        });
        phase([&] {
            if (id != 0)
                return;
            std::optional<std::pair<std::uint64_t, Vertex>> best;
            for (const auto& candidate : pivotCandidates)
                if (candidate && (!best || candidate->first > best->first))
                    best = candidate;
            havePivot = best.has_value();
            if (havePivot) {
                pivot = best->second;
                forward[pivot].store(1, std::memory_order_relaxed);
                backward[pivot].store(1, std::memory_order_relaxed);
                frontier.assign(1, pivot);
            }
        });
        if (havePivot) {
            search(id, offsets, targets, [&](Vertex w) {
                return state[w].load(std::memory_order_relaxed) == ALIVE &&
                       forward[w].load(std::memory_order_relaxed) == 0 &&
                       forward[w].exchange(1, std::memory_order_relaxed) == 0;
            });
            phase([&] {
                if (id == 0)
                    frontier.assign(1, pivot);
            });
            search(id, inOffsets, sources, [&](Vertex w) {
                return forward[w].load(std::memory_order_relaxed) == 1 &&
                       backward[w].load(std::memory_order_relaxed) == 0 &&
                       backward[w].exchange(1, std::memory_order_relaxed) == 0;
            });
            phase([&] {
                for (std::size_t v = first; v < last; ++v) {
                    if (backward[v].load(std::memory_order_relaxed) == 1) {
                        state[v].store(REMOVED, std::memory_order_relaxed);
                        giantParts[id].push_back(static_cast<Vertex>(v));
                    }
                }
            });
        }

        // Hand the rest to the Tarjan engine: removed vertices count as complete.
        phase([&] {
            for (std::size_t v = first; v < last; ++v)
                if (state[v].load(std::memory_order_relaxed) == REMOVED)
                    engine.excludeVertex(static_cast<Vertex>(v));
        });
    }

    //Removes vertices until no thread has one left. Each thread works through a stack of its
    //own and shares only when that stack grows, because removals cascade along narrow fronts
    //where threads sharing every vertex would mostly wait for one another. trimPending
    //counts the vertices in the shared queue and the threads holding vertices: a thread
    //counts itself before uncounting what it takes from the queue, and counts what it
    //shares before the queue shows it, so trimPending is 0 only when nothing is left
    void trim(unsigned id) {
        constexpr std::size_t TAKE = 64;   //Vertices taken from the shared queue at once
        constexpr std::size_t SHARE = 512; //A stack this deep gives half its vertices away
        const auto offsets = graph.offsets();
        const auto targets = graph.targets();
        moodycamel::ConsumerToken consumer(trimQueue);
        moodycamel::ProducerToken producer(trimQueue);
        std::vector<Vertex> stack(TAKE);
        auto& removed = trimmed[id];

        int idlePolls = 0;
        while (!aborted.load(std::memory_order_relaxed)) {
            stack.resize(TAKE);
            const std::size_t taken = trimQueue.try_dequeue_bulk(consumer, stack.begin(), TAKE);
            stack.resize(taken);
            if (taken == 0) {
                if (trimPending.load() == 0)
                    return;
                //A long cascade can keep one thread busy while the others have nothing:
                //back off, so that with more threads than cores they do not take its time
                if (++idlePolls < 64)
                    std::this_thread::yield();
                else
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
                continue;
            }
            idlePolls = 0;
            trimPending.fetch_add(1); //This thread now holds vertices
            trimPending.fetch_sub(static_cast<std::int64_t>(taken));

            while (!stack.empty()) {
                const Vertex v = stack.back();
                stack.pop_back();
                removed.push_back(v);
                for (std::size_t edge = offsets[v]; edge < offsets[v + 1]; ++edge) {
                    const Vertex w = targets[edge];
                    if (inDegree[w].fetch_sub(1, std::memory_order_relaxed) == 1 && tryRemove(w))
                        stack.push_back(w);
                }
                for (std::size_t edge = inOffsets[v]; edge < inOffsets[v + 1]; ++edge) {
                    const Vertex u = sources[edge];
                    if (outDegree[u].fetch_sub(1, std::memory_order_relaxed) == 1 && tryRemove(u))
                        stack.push_back(u);
                }
                if (stack.size() >= SHARE) {
                    const std::size_t share = stack.size() / 2;
                    trimPending.fetch_add(static_cast<std::int64_t>(share));
                    if (!trimQueue.enqueue_bulk(producer, stack.end() - share, share))
                        throw std::bad_alloc();
                    stack.resize(stack.size() - share);
                }
            }
            trimPending.fetch_sub(1); //Holds nothing now
        }
    }

    //Level-synchronous breadth-first search from frontier along the given CSR arrays;
    //visit(w) claims w and returns true at most once per vertex
    template <class Offsets, class Targets, class Visit>
    void search(unsigned id, const Offsets& edgeOffsets, const Targets& edgeTargets, Visit visit) {
        //Below this many vertices one thread advances the frontier faster than all of them
        //can meet at a barrier for every level. On a long path or cycle every level is
        //small, and level-synchronous search alone would take one barrier round per vertex
        constexpr std::size_t SMALL_FRONTIER = 4096;
        while (true) {
            phase([&] {
                if (id != 0)
                    return;
                std::vector<Vertex> next;
                while (!frontier.empty() && frontier.size() < SMALL_FRONTIER) {
                    next.clear();
                    for (const Vertex u : frontier)
                        for (std::size_t edge = edgeOffsets[u]; edge < edgeOffsets[u + 1]; ++edge)
                            if (visit(edgeTargets[edge]))
                                next.push_back(edgeTargets[edge]);
                    frontier.swap(next);
                }
                smallFrontierDone = frontier.empty();
            });
            if (smallFrontierDone || stopped)
                return;
            phase([&] {
                if (id == 0)
                    blocks.reset(frontier.size());
                nextFrontiers[id].clear();
            });
            phase([&] {
                std::size_t begin, end;
                auto& next = nextFrontiers[id];
                while (blocks.take(begin, end))
                    for (std::size_t i = begin; i < end; ++i) {
                        const Vertex u = frontier[i];
                        for (std::size_t edge = edgeOffsets[u]; edge < edgeOffsets[u + 1]; ++edge)
                            if (visit(edgeTargets[edge]))
                                next.push_back(edgeTargets[edge]);
                    }
            });
            phase([&] {
                if (id != 0)
                    return;
                frontier.clear();
                for (const auto& next : nextFrontiers)
                    frontier.insert(frontier.end(), next.begin(), next.end());
                levelDone = frontier.empty();
            });
            //Every thread reads the same decision after the barrier, so all leave together and
            //keep passing the same barriers. (Not frontier itself: once thread 0 has left, the
            //next phase refills it while other threads may still be deciding.)
            if (levelDone || stopped)
                return;
        }
    }
};

} // namespace

Components hybridScc(const Graph& graph, unsigned threadCount) {
    if (threadCount == 0)
        throw std::invalid_argument("hybridScc needs at least one worker thread");
    Hybrid algorithm(graph, threadCount);
    return algorithm.run();
}

} // namespace ptarjan
