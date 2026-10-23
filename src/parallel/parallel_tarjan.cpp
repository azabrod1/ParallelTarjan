//
//  parallel_tarjan.cpp
//  Tarjan
//
//  Created by Alex Zabrodskiy on 4/30/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#include "parallel_tarjan.hpp"

#include <chrono>
#include <deque>
#include <stdexcept>
#include <thread>
#include <utility>

#include "ptarjan/scc.hpp"

namespace ptarjan::detail {

namespace {

constexpr auto SPIN_BEFORE_SLEEPING = std::chrono::microseconds(200);

unsigned validThreadCount(unsigned threadCount) {
    if (threadCount == 0)
        throw std::invalid_argument("Parallel Tarjan needs at least one worker thread");
    return threadCount;
}

} // namespace

ParallelTarjan::ParallelTarjan(const Graph& _graph, unsigned num_threads, PendingQueue& _pending,
                               RootQueue& _queue)
    : NUM_THREADS(validThreadCount(num_threads)), graph(_graph),
      cells(std::make_unique<Cell[]>(_graph.vertexCount())), pending(_pending), cellQueue(_queue) {}

Components ParallelTarjan::run() {
    std::deque<Worker> workers; //Workers cannot move: other threads hold pointers into them
    for (unsigned ID = 0; ID < NUM_THREADS; ++ID)
        workers.emplace_back(ID, *this, cells.get(), graph.offsets().data(),
                             graph.targets().data());

    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);
    try {
        for (Worker& worker : workers) {
            threads.emplace_back([this, &worker] {
                try {
                    worker();
                } catch (...) {
                    fail(std::current_exception());
                }
            });
        }
    } catch (...) {
        //Could not start every thread. The ones already running stop early
        fail(std::current_exception());
    }

    //Join every worker before any is destroyed: other threads may inspect a worker's
    //searches until they all stop
    for (std::thread& thread : threads)
        thread.join();

    if (failure)
        std::rethrow_exception(failure);

    std::vector<Vertex> vertices;
    vertices.reserve(graph.vertexCount());
    std::vector<std::size_t> offsets{0};
    for (const Worker& worker : workers)
        worker.collectSCCs(vertices, offsets);

    return {std::move(vertices), std::move(offsets)};
}

void ParallelTarjan::fail(std::exception_ptr error) {
    {
        const std::scoped_lock lock(failureMutex);
        if (!failure)
            failure = std::move(error);
    }
    aborted = true;
}

/*Called by worker threads to retrieve a search to execute. We first check the Pending queue
 if any searches are ready to be resumed. If Pending is empty, we look for a cell that hasn't
 been explored yet in the worker's block of root vertices, taking a new block from the root
 queue when the old one is used up, and start a new search from it. If there are no more cells
 to explore, the thread polls until either a search is resumed or the termination protocol
 (see activeSearches) shows that no search can run again.
 Returns nullptr when the worker should exit*/
Search* ParallelTarjan::getSearch(Worker& worker) {

    //When the worker started waiting for work; unset while it has some
    std::chrono::steady_clock::time_point idleSince{};

    while (!aborted.load(std::memory_order_relaxed)) {

        //First, see if there is a pending Search we can resume. It was counted
        //in activeSearches when it was added to Pending
        if (Search* const search = pending.get(worker.getID()))
            return search;

        //Otherwise, try to start new search from the worker's block of roots
        if (worker.holdsRoots()) {

            /* We need to conquer the root before starting the search.
             * Otherwise, we could be starting a search on a cell already
             * in another search's stack! If the conquer suceeds, we can start the search
             * otherwise we continue this loop
             */
            Cell* const root = worker.nextLocalRoot();
            //Found a viable root for the new search?
            if (root && root->claimOrFail(worker.getSpareSearch())) {
                //Count the search. The block is still counted meanwhile, so the count cannot
                //touch zero; if the block is now used up, its count passes to the search
                if (worker.holdsRoots())
                    activeSearches.fetch_add(1);
                Search* const search = worker.getSpareSearch();
                search->setRoot(root);
                worker.allocateSpareSearch();
                return search;
            }

            if (!worker.holdsRoots())
                activeSearches.fetch_sub(1); //The block is used up
            continue;
        }

        if (!cellQueue.isEmpty()) {
            //Count the block before taking it, so that no worker can see zero
            //active searches while we hold roots that nobody has examined
            activeSearches.fetch_add(1);
            if (!cellQueue.nextChunk(worker, worker.rootRange()))
                activeSearches.fetch_sub(1); //Another worker took the last block
            continue;
        }

        //Every root has been handed out and nothing is running or waiting to run.
        //Suspended searches can only be resumed by running ones, so we are done
        if (activeSearches.load() == 0)
            return nullptr;

        //Searches are resumed often and briefly, so a worker that has just run out of
        //work spins for a moment before it starts sleeping. Measured on 1M-vertex
        //clustered graphs: sleeping at once cost about a quarter of the 16-thread time,
        //and spinning throughout wasted CPU for no further gain. Blocking on a condition
        //variable instead made large graphs more than twice as slow
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
