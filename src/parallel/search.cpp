#include "search.hpp"

#include <algorithm>

#include "worker.hpp"

namespace ptarjan::detail {

// Pre: the search owns the cell. Puts it on both stacks with the next index.
void Search::pushToStacks(Cell* newCell) {
    tarjanStack.push_back(newCell);
    controlStack.push_back(newCell);
    newCell->initIndex(cellCount++);
}

// After a transfer the stack's top may have come from another search.
void Search::refreshCellCount() {
    cellCount = tarjanStack.back()->index + 1;
}

/*
 Pre:     The searches S0, S1, ..., Sn form a blocking cycle, S0 --> S1 --> ... --> Sn,
          and cells are being transferred from S0 .. Sn-1 to Sn to resolve it.

 Inputs:  src is one of S0 .. Sn-1 and dest is Sn, which receives the cells.
          conflictCell is the cell of src that the search before it in the cycle is
          blocked on (Ci in the dissertation).

 Purpose: Moves cells from the top of src's Tarjan stack to dest's until both
          a) the conflict cell has been moved, and
          b) the last cell moved has an index no greater than the smallest rank moved
             so far.
          a) resolves the conflict. b) moves every cell of src in the conflict cell's
          component: otherwise src and dest would each keep part of that component,
          collide again and form another cycle.

          Calling L the last cell moved, the control stack's cells from its top down
          to L move as well. L is on src's control stack: the loop stops at L with
          L.index <= minRank <= L.rank <= L.index, so L's index equals its rank, and a
          cell that left the control stack with equal index and rank would already
          have been popped as the root of a component.

 Post:    The moved cells carry indices and ranks in dest's numbering. src is now
          blocked on L, or has no cells left.
*/
void Search::transferCells(Worker& worker, Search* const src, Search* const dest,
                           Cell* const conflictCell) {
    TarjanStack& srcTarjanStack = src->tarjanStack;
    ControlStack& srcControlStack = src->controlStack;

    auto reverseIt = srcTarjanStack.rbegin();
    Cell* next = *reverseIt;
    Vertex minRank = next->rank;
    bool reachedConflict = next == conflictCell;

    while (!reachedConflict || next->index > minRank) {
        next = *(++reverseIt);
        minRank = std::min(minRank, next->rank);
        if (next == conflictCell)
            reachedConflict = true;
    }

    Cell* const last = next;
    const Vertex delta = dest->cellCount - last->index;

    const auto moved = (reverseIt + 1).base();
    dest->tarjanStack.insert(dest->tarjanStack.end(), moved, srcTarjanStack.end());
    for (auto it = moved; it != srcTarjanStack.end(); ++it)
        (*it)->transfer(delta, dest);
    srcTarjanStack.erase(moved, srcTarjanStack.end());

    auto rit = srcControlStack.rbegin();
    while (*rit != last)
        ++rit;
    dest->controlStack.insert(dest->controlStack.end(), (rit + 1).base(), srcControlStack.end());
    srcControlStack.erase((rit + 1).base(), srcControlStack.end());

    // src now waits for the deepest of its former cells.
    if (!srcTarjanStack.empty()) {
        src->suspendOn(last);
        worker.blockedListOf(last)->push_back(src);
        src->refreshCellCount();
    } else {
        src->clearBlockedOn();
    }

    dest->refreshCellCount();
}

} // namespace ptarjan::detail
