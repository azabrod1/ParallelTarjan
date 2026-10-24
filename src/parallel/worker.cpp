#include "worker.hpp"

#include <algorithm>
#include <utility>

#include "parallel_tarjan.hpp"

namespace ptarjan::detail {

Worker::Worker(unsigned identifier, ParallelTarjan& owner, Cell* cellArray,
               const std::size_t* edgeOffsets, const Vertex* edgeTargets)
    : scheduler(owner), cells(cellArray), offsets(edgeOffsets), targets(edgeTargets),
      workerId(identifier), spare(&allocatedSearches.emplace_back()) {}

void Worker::operator()() {
    while (Search* const search = scheduler.getSearch(*this)) {
        execute(search);
        scheduler.searchStopped(); // The search finished or suspended
    }
}

void Worker::execute(Search* const search) {
    while (!search->done()) {
        Cell* const curr = search->controlStackTop();
        const Vertex vertex = vertexOf(curr);

        if (curr->explored != offsets[vertex + 1] - offsets[vertex]) {
            Cell* const child = nextSuccessor(curr, search);

            // One load settles the common cases. claim() and suspend() check again, the
            // latter with the sequentially consistent ordering the suspension protocol needs.
            Search* const owner = child->status.load(std::memory_order_acquire);

            if (owner == CellStatus::completeCell())
                continue;

            if (owner == search) {
                curr->promote(child->index); // On our stack: update the low link
                continue;
            }

            // The child may have changed state since the load, so claim() decides.
            const Claim attempt = child->claim(search);
            if (attempt == Claim::CLAIMED)
                search->pushToStacks(child);
            else if (attempt == Claim::OCCUPIED && scheduler.suspend(*this, search, child))
                return;
        } else {
            // Every successor explored: backtrack.
            search->controlStackPop();

            if (!search->done())
                search->controlStackTop()->promote(curr->rank);

            if (curr->index == curr->rank) {
                if (curr == search->tarjanStackTop())
                    buildSingletonSCC(search, curr);
                else
                    buildSCC(search, curr);
            }
        }
    }

    // The control stack is empty, so the search is finished and its object can be reused.
    reclaim(search);
}

// Returns a successor of curr to explore and marks it explored. Prefers one that no
// other search holds, to avoid a suspension.
// Pre: curr is owned by search and has a successor left to explore.
Cell* Worker::nextSuccessor(Cell* const curr, Search* const search) {
    const Vertex vertex = vertexOf(curr);
    const Vertex* const front = targets + offsets[vertex] + curr->explored; // Not handed out yet
    const std::size_t remaining = offsets[vertex + 1] - offsets[vertex] - curr->explored;

    // If another search holds the next successor, look for a free one among the next few.
    // (Looking further makes high-degree vertices quadratic.) The check is unsynchronized,
    // so the cell returned may be occupied after all; that only costs a suspension.
    constexpr std::size_t LOOKAHEAD = 16; // At most 32, the bits in exploredAhead
    if (cell(front[0])->occupiedByOther(search)) {
        const std::size_t window = std::min(remaining, LOOKAHEAD + 1);
        for (std::size_t ahead = 1; ahead < window; ++ahead) {
            const std::uint32_t bit = std::uint32_t{1} << (ahead - 1);
            if (!(curr->exploredAhead & bit) && !cell(front[ahead])->occupiedByOther(search)) {
                curr->exploredAhead |= bit;
                return cell(front[ahead]);
            }
        }
    }

    // Hand out the front successor, then step past any that were handed out out of turn.
    bool handedOut = true;
    while (handedOut) {
        handedOut = (curr->exploredAhead & 1U) != 0;
        curr->exploredAhead >>= 1U;
        ++curr->explored;
    }
    return cell(front[0]);
}

void Worker::buildSCC(Search* const search, Cell* const head) {
    TarjanStack& tarjanStack = search->getTS();

    tarjanStack.back()->markComplete();
    auto rit = tarjanStack.rbegin();
    while (*rit != head)
        (*(++rit))->markComplete();

    // Every cell of the component is complete before any search blocked on one resumes.
    for (auto it = (rit + 1).base(); it != tarjanStack.end(); ++it) {
        scheduler.resumeAllBlockedOn(workerId, *it, toResume);
        found.add(vertexOf(*it));
    }
    found.endComponent();

    tarjanStack.erase((rit + 1).base(), tarjanStack.end());
}

void Worker::buildSingletonSCC(Search* const search, Cell* const cell) {
    search->tarjanStackPop();
    cell->markComplete();
    scheduler.resumeAllBlockedOn(workerId, cell, toResume);
    found.addSingleton(vertexOf(cell));
}

SearchList* Worker::blockedListOf(Cell* const cell) {
    SearchList* list = cell->blockedList();
    if (list)
        return list;
    if (!spareList)
        spareList = std::make_unique<SearchList>();
    if (ownedLists.size() == ownedLists.capacity()) // So that recording ownership cannot throw
        ownedLists.reserve(2 * ownedLists.size() + 16);
    if (cell->installBlockedList(spareList.get(), list))
        ownedLists.push_back(std::move(spareList));
    return list;
}

void Worker::allocateSpareSearch() {
    if (recycled.empty()) {
        spare = &allocatedSearches.emplace_back();
    } else {
        spare = recycled.back();
        recycled.pop_back();
    }
}

} // namespace ptarjan::detail
