#include "parallel_tarjan.hpp"

#include <chrono>
#include <deque>
#include <thread>
#include <utility>

#include "ptarjan/scc.hpp"

namespace ptarjan::detail {

namespace {

constexpr auto SPIN_BEFORE_SLEEPING = std::chrono::microseconds(200);

} // namespace

ParallelTarjan::ParallelTarjan(const Graph& input, unsigned workers, PendingQueue& pendingQueue,
                               RootQueue& rootQueue)
    : threadCount(checkedThreadCount(workers, "parallelScc")), graph(input),
      cells(std::make_unique<Cell[]>(input.vertexCount())), pending(pendingQueue),
      roots(rootQueue) {}

Components ParallelTarjan::run() {
    std::deque<Worker> workers; // Workers cannot move: other threads hold pointers into them
    for (unsigned id = 0; id < threadCount; ++id)
        workers.emplace_back(id, *this, cells.get(), graph.offsets().data(),
                             graph.targets().data());

    std::vector<std::thread> threads;
    threads.reserve(threadCount);
    try {
        for (Worker& worker : workers) {
            threads.emplace_back([this, &worker] {
                try {
                    worker();
                } catch (...) {
                    failure.record(std::current_exception());
                }
            });
        }
    } catch (...) {
        // Could not start every thread. The ones already running stop early.
        failure.record(std::current_exception());
    }

    // Join every worker before any is destroyed: other threads may inspect a worker's
    // searches until they all stop.
    for (std::thread& thread : threads)
        thread.join();

    failure.rethrowIfAny();

    ComponentsBuilder result;
    result.reserve(graph.vertexCount());
    for (const Worker& worker : workers)
        result.append(worker.components());
    return std::move(result).build();
}

// The next search for the worker to execute: a resumed one from Pending if any is waiting,
// otherwise a new search from the next new cell in the worker's block of roots, taking a
// new block from the root queue when the old one is used up. With neither, the worker
// polls until a search is resumed or the termination protocol (see activeSearches) shows
// that no search can run again. Returns nullptr when the worker should exit.
Search* ParallelTarjan::getSearch(Worker& worker) {

    // When the worker started waiting for work; unset while it has some.
    std::chrono::steady_clock::time_point idleSince{};

    while (!failure.aborted(std::memory_order_relaxed)) {

        // A resumed search was counted when it was added to Pending.
        if (Search* const search = pending.get(worker.id()))
            return search;

        if (worker.holdsRoots()) {
            // The root must be claimed first: another search may have reached it since
            // nextLocalRoot() saw it new.
            Cell* const root = worker.nextLocalRoot();
            if (root && root->tryClaim(worker.spareSearch())) {
                // Count the search. The block is still counted meanwhile, so the count cannot
                // touch zero; if the block is now used up, its count passes to the search.
                if (worker.holdsRoots())
                    activeSearches.fetch_add(1);
                Search* const search = worker.spareSearch();
                search->push(root);
                worker.allocateSpareSearch();
                return search;
            }

            if (!worker.holdsRoots())
                activeSearches.fetch_sub(1); // The block is used up
            continue;
        }

        if (!roots.isEmpty()) {
            // Count the block before taking it, so that no worker can see zero
            // active searches while we hold roots that nobody has examined.
            activeSearches.fetch_add(1);
            if (!roots.nextBlock(worker, worker.rootRange()))
                activeSearches.fetch_sub(1); // Another worker took the last block
            continue;
        }

        // Every root has been handed out and nothing is running or waiting to run.
        // Suspended searches can only be resumed by running ones, so we are done.
        if (activeSearches.load() == 0)
            return nullptr;

        // Searches are resumed often and briefly, so a worker that has just run out of
        // work spins for a moment before it starts sleeping. Measured on 1M-vertex
        // clustered graphs: sleeping at once cost about a quarter of the 16-thread time,
        // and spinning throughout wasted CPU for no further gain. Blocking on a condition
        // variable instead made large graphs more than twice as slow.
        const auto now = std::chrono::steady_clock::now();
        if (idleSince == std::chrono::steady_clock::time_point{})
            idleSince = now;
        if (now - idleSince < SPIN_BEFORE_SLEEPING)
            std::this_thread::yield();
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return nullptr;
}

} // namespace ptarjan::detail

namespace ptarjan {

unsigned defaultThreadCount() noexcept {
    const unsigned hardware = std::thread::hardware_concurrency();
    return hardware == 0 ? 1 : hardware;
}

Components parallelScc(const Graph& graph, unsigned threadCount) {
    detail::LockFreePendingQueue pending(threadCount);
    detail::VertexOrderRoots roots(graph.vertexCount(), threadCount);
    detail::ParallelTarjan algorithm(graph, threadCount, pending, roots);
    return algorithm.run();
}

} // namespace ptarjan
