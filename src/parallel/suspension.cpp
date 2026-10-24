#include "suspension.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>

#include "worker.hpp"

namespace ptarjan::detail {

namespace {

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
void transferCells(Worker& worker, Search* const src, Search* const dest,
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

/*
 Inputs:  S is the confirmed cycle S0 -> S1 -> ... -> Sn from suspend(), and C[i] is
          the cell of S[i] that the search before it is blocked on (Sn waits on C0).

 Purpose: The searches of a blocking cycle all hold cells of one component, the
          conflict component, and a cell becomes complete only when a single search
          holds its whole component. Moving the conflict component's cells to Sn ends
          the cycle; transferCells explains which cells that takes.

 Post:    The cells of S0 .. Sn-1 known to be in the conflict component belong to Sn.
          Each of S0 .. Sn-1 is blocked on the last cell it gave up. The top of Sn's
          control stack has its rank lowered to cover the transferred cells.
*/
void resolveCycle(Worker& worker, const std::vector<Search*>& S, const std::vector<Cell*>& C) {
    Search* const Sn = S.back();

    for (std::size_t i = 0; i + 1 < S.size(); ++i)
        transferCells(worker, S[i], Sn, C[i]);

    // The transferred cells and Cn are in one component: the new top of Sn's stack takes
    // the index of the cell of Sn that Sn-1 was blocked on as its rank.
    Sn->controlStack.back()->promote(C.back()->index);
}

} // namespace

/*
 Pre:     Sn reached conflictCell, which another search holds, and Sn is not blocked
          on any cell.

 Purpose: Suspends Sn until conflictCell is assigned to a component, and resolves the
          blocking cycle that the suspension may close. When several searches take cells
          of one component, each ends up waiting for a cell another holds; none can
          finish the component, so they would wait forever.

          Cycle detection. Starting from conflictCell's owner S0, follow each search to
          the owner of the cell it is blocked on: S0 -> S1 -> ... The walk stops with no
          cycle at a search that is not blocked, and finds a candidate cycle when it
          returns to Sn. S records the searches, C the cells (C[i] is the cell of S[i]
          that the search before it waits on) and L each search's age:

          S0 ----> S1 ----> S2 ---> ... ---> Sn-1 ----> Sn ----> (back to S0)
           ^        ^        ^                ^          ^
           |        |        |                |          |
           C0       C1       C2               Cn-1       Cn

          A second walk confirms the cycle. If any search, cell or age differs, or a cell
          has become complete or changed owner, Sn's suspension did not close a cycle and
          Sn simply stays suspended. A search never suspends twice on the same cell, so a
          path that is unchanged on the second walk was blocked throughout: the cycle is
          real. Several threads may confirm the same cycle at once; the one that makes
          the age of the cycle's search at the lowest address odd, with a CAS, resolves
          it. An odd age tells other walks that a resolution is under way.

 Post:    With no cycle, Sn is suspended on conflictCell. With a cycle, the cells of the
          conflicting component have been transferred to Sn, which now owns the cell it
          was blocked on and continues. Searches of the cycle left with no cells are
          reclaimed for reuse.

 Returns: WAIT if Sn is suspended, CONTINUE if it can continue at once.

 Note: the dissertation version also recorded the age of each cell Ci, because cells were
 recycled to represent other vertices. Every vertex now has its own cell for the whole
 run, so a cell cannot change identity between the two walks.
*/
AfterSuspend suspend(Worker& worker, Search* const Sn, Cell* const conflictCell) {
    const std::uint32_t SnAge = Sn->age;

    worker.blockedListOf(conflictCell)->push_back(Sn);
    Sn->suspendOn(conflictCell);

    // The cell may have become complete before Sn was on its blocked list, in which case
    // the completing thread may not have resumed Sn. Take Sn back if it is still blocked;
    // if the CAS fails, the completing thread has queued Sn and another worker may already
    // be running it, so this one must not.
    if (conflictCell->isComplete())
        return Sn->tryClearBlockedOn(conflictCell) ? AfterSuspend::CONTINUE : AfterSuspend::WAIT;

    // Reused buffers, so that suspending does not allocate.
    BlockingPath& path = worker.blockingPath();
    path.clear();
    auto& [S, C, L] = path;

    // First walk.
    Search* Si = Sn;
    Cell* Ci = nullptr;
    std::uint32_t Li = 0;

    // The path can lead into a blocking cycle that does not contain Sn. That cycle is
    // resolved by its own last suspender, but until then the walk below would go round
    // it indefinitely, growing S, C and L. Brent's cycle detection spots a repeated
    // search: remember the search at each power-of-two step and stop if we meet it again.
    Search* checkpoint = nullptr;
    std::size_t nextCheckpoint = 1;

    do {
        Ci = Si->blockedOn();
        if (!Ci)
            return AfterSuspend::WAIT;

        Si = Ci->owner();
        if (!Si)
            return AfterSuspend::WAIT;

        // An odd age means a thread is resolving a cycle through Si.
        Li = Si->age;
        if (Li & 1U)
            return AfterSuspend::WAIT;

        // Back at a search we already passed without meeting Sn: Sn is not on a cycle.
        if (Si == checkpoint)
            return AfterSuspend::WAIT;

        C.push_back(Ci);
        S.push_back(Si);
        L.push_back(Li);

        if (S.size() == nextCheckpoint) {
            checkpoint = Si;
            nextCheckpoint *= 2;
        }
    } while (Si != Sn);

    // Second walk.
    Si = Sn;
    std::size_t minPtr = 0; // The index of the search at the lowest address
    const std::size_t pathSize = S.size();

    for (std::size_t i = 0; i < pathSize; ++i) {
        Ci = Si->blockedOn();

        // The age must not have changed since the first walk read Si's blocking cell.
        if (Si->age != L[(i + pathSize - 1) % pathSize])
            return AfterSuspend::WAIT;

        if (Ci != C[i])
            return AfterSuspend::WAIT;

        Si = Ci->owner(); // nullptr if Ci is complete
        if (Si != S[i])
            return AfterSuspend::WAIT;

        // std::less supplies a total order even for separately allocated
        // searches; built-in relational pointer comparison does not.
        if (std::less<Search*>{}(Si, S[minPtr]))
            minPtr = i;
    }

    // If Sn suspended on a different cell, or finished and was reused, a later call
    // to suspend() handles any cycle it is on.
    if (Sn->blockedOn() != conflictCell || SnAge != L[pathSize - 1])
        return AfterSuspend::WAIT;

    // The cycle is confirmed. Only the thread whose CAS makes S[minPtr]'s age odd resolves it.
    if (!S[minPtr]->age.compare_exchange_strong(L[minPtr], L[minPtr] + 1))
        return AfterSuspend::WAIT;

    // Sn stops waiting before the transfer so that no walk during it sees a false cycle.
    Sn->clearBlockedOn();

    resolveCycle(worker, S, C);

    ++S[minPtr]->age; // Even again: the resolution is over

    for (Search* search : S)
        if (search->done())
            worker.reclaim(search);

    return AfterSuspend::CONTINUE;
}

/*
 Collects the searches to resume now that completeCell is complete. The blocked list can
 also hold searches that stopped waiting on the cell for another reason, during a cycle
 resolution, and so are no longer suspended on it; the list has no delete operation.
 Each search is taken only if a CAS clears completeCell as its blocking cell, so a
 search that has moved on is never resumed by mistake.

*/
void collectResumable(const SearchList& blocked, Cell* const completeCell,
                      std::vector<Search*>& toResume) {
    blocked.forEach([&](Search* const search) {
        if (search->tryClearBlockedOn(completeCell))
            toResume.push_back(search);
    });
}

} // namespace ptarjan::detail
