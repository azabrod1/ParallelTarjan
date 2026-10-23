//
//  parallel_tarjan.hpp
//  Tarjan
//
//  Created by Alex Zabrodskiy on 4/30/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#ifndef PTARJAN_PARALLEL_TARJAN_HPP
#define PTARJAN_PARALLEL_TARJAN_HPP

#include <atomic>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <vector>

#include "cell.hpp"
#include "pending_queue.hpp"
#include "ptarjan/components.hpp"
#include "ptarjan/graph.hpp"
#include "root_queue.hpp"
#include "search.hpp"
#include "suspension_manager.hpp"
#include "worker.hpp"

namespace ptarjan::detail {

//Runs the parallel algorithm once. The pending and root queues are parameters so
//that tests can substitute instrumented or ThreadSanitizer-friendly versions
class ParallelTarjan {

private:
    //Constants
    const unsigned NUM_THREADS;

    //Data structures
    const Graph& graph;
    std::unique_ptr<Cell[]> cells;
    SuspensionManager susMgr;
    PendingQueue& pending;
    RootQueue& cellQueue;

    //Member Variables

    /* Termination protocol. activeSearches counts the searches that are running
     * or waiting in Pending to be resumed, plus the workers holding a block of
     * root vertices they have not finished examining. Suspended searches are not
     * counted: only a running search can complete the cell a suspended search
     * waits on. A search is counted before it is added to Pending and uncounted
     * only after its worker stops executing it, and a block is counted before it
     * is taken from the root queue, so the count cannot reach zero while any
     * search can still become runnable. Once the root queue is exhausted and the
     * count is zero, no search can ever run again and every worker may exit.
     */
    std::atomic<std::int64_t> activeSearches{0};

    //Set if a worker throws (in practice, std::bad_alloc). The other workers then
    //stop taking searches, and run() rethrows the first exception
    std::atomic<bool> aborted{false};
    std::mutex failureMutex;
    std::exception_ptr failure;

    void fail(std::exception_ptr error);

public:
    ParallelTarjan(const Graph& _graph, unsigned num_threads, PendingQueue& _pending,
                   RootQueue& _queue);

    Components run();

    Search* getSearch(Worker& worker);

    //Called by a worker after execute() returns, whether its search finished or suspended
    void searchStopped() { activeSearches.fetch_sub(1); }

    bool suspend(Worker& worker, Search* const S, Cell* conflictCell) {
        return susMgr.suspend(worker, S, conflictCell);
    }

    void resumeAllBlockedOn(unsigned worker, Cell* const completeCell,
                            std::vector<Search*>& toResume) {
        SearchList* const suspendedList(completeCell->getBlockedList());

        if (suspendedList && suspendedList->size()) {
            toResume.clear();
            susMgr.bulkUnsuspend(suspendedList, completeCell, toResume);

            if (!toResume.empty()) {
                //Count the searches before they become visible to other workers
                activeSearches.fetch_add(static_cast<std::int64_t>(toResume.size()));
                pending.addPending(worker, toResume);
            }
        }
    }
};

} // namespace ptarjan::detail

#endif
