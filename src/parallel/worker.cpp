//
//  worker.cpp
//  Tarjan
//
//  Created by Alex Zabrodskiy on 5/10/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#include "worker.hpp"

#include <algorithm>
#include <utility>

#include "parallel_tarjan.hpp"

namespace ptarjan::detail {

Worker::Worker(unsigned id, ParallelTarjan& _scheduler, Cell* _cells, const std::size_t* _offsets,
               const Vertex* _targets)
    : scheduler(_scheduler), cells(_cells), offsets(_offsets), targets(_targets), ID(id),
      spareSearch(&allocatedSearches.emplace_back()) {}

void Worker::operator()() {
    while (Search* const search = scheduler.getSearch(*this)) {
        execute(search);
        scheduler.searchStopped(); //The search finished or suspended
    }
}

void Worker::execute(Search* const search) {

    while (!search->controlStackEmpty()) {

        Cell* const curr = search->controlStackTop();

        const Vertex vertex = vertexOf(curr);
        if (curr->explored != offsets[vertex + 1] - offsets[vertex]) {

            Cell* const child = nextSuccessor(curr, search);

            //One load settles the common cases. claim() and suspend() check again, the
            //latter with the sequentially consistent ordering the suspension protocol needs
            Search* const owner = child->status.load(std::memory_order_acquire);

            //No search has to visit a complete cell again
            if (owner == CellStatus::completeCell())
                continue;

            if (owner == search) //if on stack, update low link
                curr->promote(child->index);

            //Below conditional not synchronized, so need to handle the possible
            //case that the cell becomes complete after the conditional but before conquer

            else {
                const CellStatus::Claim attempt = child->claim(search);

                if (attempt == CellStatus::Claim::CLAIMED)
                    search->pushToStacks(
                        child); //Put child on Tarjan and Control stack and assign cell an index

                else if (attempt == CellStatus::Claim::OCCUPIED &&
                         scheduler.suspend(*this, search, child))
                    return;

                //Do not need to do anything if cell is complete
            }

        } else { //Cell has no more children left, time to backtrack

            search->controlStackPop();

            if (!search->controlStackEmpty())
                search->controlStackTop()->promote(curr->rank);

            if (curr->index == curr->rank) {
                if (curr == search->tarjanStackTop())
                    buildSingletonSCC(search, curr);
                else
                    buildSCC(search, curr);
            }
        }
    }
    //We can only get here if the search's control stack becomes empty. Thus,
    //we can reclaim the memory from the finished search at this point

    reclaim(search);
}

/*Returns a successor of curr to be explored and marks it explored.
 The function tries to choose a cell that is not currently
 being explored by another search if one exists, to avoid a suspension.
 Pre: curr is owned by search and has a successor left to explore*/
Cell* Worker::nextSuccessor(Cell* const curr, Search* const search) {
    const Vertex vertex = vertexOf(curr);
    const Vertex* const front = targets + offsets[vertex] + curr->explored; //Not handed out yet
    const std::size_t remaining = offsets[vertex + 1] - offsets[vertex] - curr->explored;

    //If the next successor is owned by another search, try to find one that is not among
    //the next few. (Looking further makes high-degree vertices quadratic.)
    //If none is found, we have no choice but to return an occupied cell.
    //Note that this is not sychronized, so the cell might become occupied by the time
    //we return. This is ok since we make no guarantee that we return an unoccupied cell,
    //we just try to do so as an optimization
    constexpr std::size_t LOOKAHEAD = 16; //At most 32, the bits in exploredAhead
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

    //Hand out the front successor, then step past any that were handed out out of turn
    bool handedOut = true;
    while (handedOut) {
        handedOut = (curr->exploredAhead & 1U) != 0;
        curr->exploredAhead >>= 1U;
        ++curr->explored;
    }
    return cell(front[0]);
}

void Worker::buildSCC(Search* const search, Cell* const head) {
    TarjanStack& tarjanStack(search->getTS());

    tarjanStack.back()->markComplete();
    auto rit(tarjanStack.rbegin());

    while (*rit != head)
        (*(++rit))->markComplete();

    //Every cell of the SCC is complete before we resume the searches blocked on any of them
    for (auto it = (rit + 1).base(); it != tarjanStack.end(); ++it) {
        scheduler.resumeAllBlockedOn(ID, *it, toResume);
        sccVertices.push_back(vertexOf(*it));
    }
    sccEnds.push_back(sccVertices.size());

    tarjanStack.erase((rit + 1).base(), tarjanStack.end());
}

void Worker::buildSingletonSCC(Search* const search, Cell* const cell) {
    search->tarjanStackPop();
    cell->markComplete();
    scheduler.resumeAllBlockedOn(ID, cell, toResume);
    sccVertices.push_back(vertexOf(cell));
    sccEnds.push_back(sccVertices.size());
}

SearchList* Worker::blockedListOf(Cell* const cell) {
    SearchList* list = cell->getBlockedList();
    if (list)
        return list;
    if (!spareList)
        spareList = std::make_unique<SearchList>();
    if (ownedLists.size() == ownedLists.capacity()) //So that recording ownership cannot throw
        ownedLists.reserve(2 * ownedLists.size() + 16);
    if (cell->installBlockedList(spareList.get(), list))
        ownedLists.push_back(std::move(spareList));
    return list;
}

void Worker::allocateSpareSearch() {
    if (recycled.empty())
        spareSearch = &allocatedSearches.emplace_back();
    else {
        spareSearch = recycled.back();
        recycled.pop_back();
    }
}

void Worker::collectSCCs(std::vector<Vertex>& vertices,
                         std::vector<std::size_t>& componentOffsets) const {
    const std::size_t base = vertices.size();
    vertices.insert(vertices.end(), sccVertices.begin(), sccVertices.end());
    for (const std::size_t end : sccEnds)
        componentOffsets.push_back(base + end);
}

} // namespace ptarjan::detail
