#ifndef PTARJAN_WORKER_HPP
#define PTARJAN_WORKER_HPP

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include "cell.hpp"
#include "components_builder.hpp"
#include "root_queue.hpp"
#include "search.hpp"

namespace ptarjan::detail {

class ParallelTarjan;

// The blocking path that suspend() walks, in the dissertation's notation: searches S,
// the cells C they are blocked on, and the searches' ages L.
struct BlockingPath {
    std::vector<Search*> searches;
    std::vector<Cell*> cells;
    std::vector<std::uint32_t> ages;

    void clear() {
        searches.clear();
        cells.clear();
        ages.clear();
    }
};

// The state of one worker thread. Workers are created before any thread starts and
// destroyed after every thread has been joined, so other workers may hold pointers
// to the searches a worker allocated for the whole run.
class Worker {
    ParallelTarjan& scheduler;
    Cell* const cells;
    const std::size_t* const offsets; // The graph in CSR form; see ptarjan::Graph
    const Vertex* const targets;
    const unsigned workerId;

    ComponentsBuilder found; // The components this worker found

    std::deque<Search>
        allocatedSearches; // Owns every search this worker created; a deque never moves them
    std::vector<Search*> recycled;
    Search* spare; // The search the next root starts, already claimed or allocated

    std::vector<Search*> toResume;

    // The blocked lists this worker installed, and one allocated for the next install.
    std::vector<std::unique_ptr<SearchList>> ownedLists;
    std::unique_ptr<SearchList> spareList;

    RootRange roots; // The block of vertices this worker is scanning for new roots

    BlockingPath path; // Reused by every suspend(), so that suspending does not allocate

    void execute(Search* const search);

    Cell* nextSuccessor(Cell* const curr, Search* const search);

    void buildSCC(Search* const search, Cell* const head);

    void buildSingletonSCC(Search* const search, Cell* const cell);

public:
    Worker(unsigned identifier, ParallelTarjan& owner, Cell* cellArray,
           const std::size_t* edgeOffsets, const Vertex* edgeTargets);

    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    void operator()();

    void reclaim(Search* const search) {
        search->reset();
        recycled.push_back(search);
    }

    unsigned id() const { return workerId; }

    Cell* cell(Vertex vertex) const { return cells + vertex; }

    Vertex vertexOf(const Cell* const cell) const { return static_cast<Vertex>(cell - cells); }

    // The cell's blocked list, installing one if no search has blocked on the cell before.
    SearchList* blockedListOf(Cell* const cell);

    Search* spareSearch() const { return spare; }

    RootRange& rootRange() { return roots; }

    // The next cell of the worker's root block that is still new, or nullptr once the
    // block is used up.
    Cell* nextLocalRoot() {
        while (roots.next < roots.end) {
            Cell* const candidate = cell(static_cast<Vertex>(roots.next++));
            if (candidate->isNew())
                return candidate;
        }
        return nullptr;
    }

    bool holdsRoots() const { return roots.next < roots.end; }

    void allocateSpareSearch();

    const ComponentsBuilder& components() const { return found; }

    BlockingPath& blockingPath() { return path; }
};

} // namespace ptarjan::detail

#endif
