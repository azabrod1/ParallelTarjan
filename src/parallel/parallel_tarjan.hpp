#ifndef PTARJAN_PARALLEL_TARJAN_HPP
#define PTARJAN_PARALLEL_TARJAN_HPP

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "cell.hpp"
#include "pending_queue.hpp"
#include "ptarjan/components.hpp"
#include "ptarjan/graph.hpp"
#include "root_queue.hpp"
#include "search.hpp"
#include "suspension.hpp"
#include "threading.hpp"
#include "worker.hpp"

namespace ptarjan::detail {

// Runs the parallel algorithm once. The pending and root queues are parameters so
// that tests can substitute instrumented or ThreadSanitizer-friendly versions.
class ParallelTarjan {
    const unsigned threadCount;
    const Graph& graph;
    std::unique_ptr<Cell[]> cells;
    PendingQueue& pending;
    RootQueue& roots;

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

    // If a worker throws, the others stop taking searches and run() rethrows.
    FirstFailure failure;

public:
    ParallelTarjan(const Graph& input, unsigned workers, PendingQueue& pendingQueue,
                   RootQueue& rootQueue);

    [[nodiscard]] Components run();

    // Before run() starts: treats the vertex as already assigned to a component. The caller
    // must exclude whole components only, and order these writes before run(), for
    // example by joining the threads that make them.
    void excludeVertex(Vertex vertex) { cells[vertex].markExcluded(); }

    Search* getSearch(Worker& worker);

    // Called by a worker after execute() returns, whether its search finished or suspended.
    void searchStopped() { activeSearches.fetch_sub(1); }

    // Queues the searches blocked on a cell that has just become complete. Most cells have
    // no blocked list, so that case is settled here, inline.
    void resumeAllBlockedOn(unsigned worker, Cell* const completeCell,
                            std::vector<Search*>& toResume) {
        SearchList* const blocked = completeCell->blockedList();
        if (blocked && blocked->size()) {
            toResume.clear();
            collectResumable(*blocked, completeCell, toResume);
            if (!toResume.empty()) {
                // Count the searches before they become visible to other workers.
                activeSearches.fetch_add(static_cast<std::int64_t>(toResume.size()));
                pending.addPending(worker, toResume);
            }
        }
    }
};

} // namespace ptarjan::detail

#endif
