#include "suspension_manager.hpp"

#include <cstddef>
#include <functional>

namespace ptarjan::detail {

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

 Returns: SUSPEND if Sn is suspended, RESUME if it can continue at once.

 Note: the dissertation version also recorded the age of each cell Ci, because cells were
 recycled to represent other vertices. Every vertex now has its own cell for the whole
 run, so a cell cannot change identity between the two walks.
*/
bool SuspensionManager::suspend(Worker& worker, Search* const Sn, Cell* const conflictCell) {
    const std::uint32_t SnAge = Sn->age;

    worker.blockedListOf(conflictCell)->push_back(Sn);
    Sn->suspendOn(conflictCell);

    // The cell may have become complete before Sn was on its blocked list, in which case
    // the completing thread may not have resumed Sn. Take Sn back if it is still blocked;
    // if the CAS fails, the completing thread has queued Sn and another worker may already
    // be running it, so this one must not.
    if (conflictCell->isComplete())
        return Sn->tryClearBlockedOn(conflictCell) ? RESUME : SUSPEND;

    // Reused buffers, so that suspending does not allocate.
    worker.cleanPaths();
    std::vector<Search*>& S = worker.S;
    std::vector<Cell*>& C = worker.C;
    std::vector<std::uint32_t>& L = worker.L;

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
            return SUSPEND;

        Si = Ci->owner();
        if (!Si)
            return SUSPEND;

        // An odd age means a thread is resolving a cycle through Si.
        Li = Si->age;
        if (Li & 1U)
            return SUSPEND;

        // Back at a search we already passed without meeting Sn: Sn is not on a cycle.
        if (Si == checkpoint)
            return SUSPEND;

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
            return SUSPEND;

        if (Ci != C[i])
            return SUSPEND;

        Si = Ci->owner(); // nullptr if Ci is complete
        if (Si != S[i])
            return SUSPEND;

        // std::less supplies a total order even for separately allocated
        // searches; built-in relational pointer comparison does not.
        if (std::less<Search*>{}(Si, S[minPtr]))
            minPtr = i;
    }

    // If Sn suspended on a different cell, or finished and was reused, a later call
    // to suspend() handles any cycle it is on.
    if (Sn->blockedOn() != conflictCell || SnAge != L[pathSize - 1])
        return SUSPEND;

    // The cycle is confirmed. Only the thread whose CAS makes S[minPtr]'s age odd resolves it.
    if (!S[minPtr]->age.compare_exchange_strong(L[minPtr], L[minPtr] + 1))
        return SUSPEND;

    // Sn stops waiting before the transfer so that no walk during it sees a false cycle.
    Sn->clearBlockedOn();

    runCellTransfer(worker, S, C);

    ++S[minPtr]->age; // Even again: the resolution is over

    for (Search* search : S)
        if (search->done())
            worker.reclaim(search);

    return RESUME;
}

/*
 Inputs:  S is the confirmed cycle S0 -> S1 -> ... -> Sn from suspend(), and C[i] is
          the cell of S[i] that the search before it is blocked on (Sn waits on C0).

 Purpose: The searches of a blocking cycle all hold cells of one component, the
          conflict component, and a cell becomes complete only when a single search
          holds its whole component. Moving the conflict component's cells to Sn ends
          the cycle; Search::transferCells explains which cells that takes.

 Post:    The cells of S0 .. Sn-1 known to be in the conflict component belong to Sn.
          Each of S0 .. Sn-1 is blocked on the last cell it gave up. The top of Sn's
          control stack has its rank lowered to cover the transferred cells.
*/
void SuspensionManager::runCellTransfer(Worker& worker, const std::vector<Search*>& S,
                                        const std::vector<Cell*>& C) {
    Search* const Sn = S.back();

    for (std::size_t i = 0; i + 1 < S.size(); ++i)
        Search::transferCells(worker, S[i], Sn, C[i]);

    // The transferred cells and Cn are in one component: the new top of Sn's stack takes
    // the index of the cell of Sn that Sn-1 was blocked on as its rank.
    Sn->controlStackTop()->promote(C.back()->index);
}

/*
 Collects the searches to resume now that completeCell is complete. The blocked list can
 also hold searches that stopped waiting on the cell for another reason, during a cycle
 resolution, and so are no longer suspended on it; the list has no delete operation.
 Each search is taken only if a CAS clears completeCell as its blocking cell, so a
 search that has moved on is never resumed by mistake.

 Most cells have only a few searches blocked on them, so the list's first buffer is read
 directly; longer lists need the iterator.
*/
void SuspensionManager::bulkUnsuspend(const SearchList* const blockedList, Cell* const completeCell,
                                      std::vector<Search*>& toResume) {
    Search* search = nullptr;
    const std::size_t size = blockedList->size();

    if (blockedList->isSmallList(size)) {
        const std::atomic<Search*>* const suspendedSearches = blockedList->getSmallBuffer();
        for (std::size_t e = 0; e < size; ++e) {
            search = suspendedSearches[e].load();
            if (search && search->tryClearBlockedOn(completeCell))
                toResume.push_back(search);
        }
    } else {
        auto suspendedListIt = blockedList->getIt(size);
        while (suspendedListIt.increment()) {
            search = suspendedListIt.getNext();
            if (search && search->tryClearBlockedOn(completeCell))
                toResume.push_back(search);
        }
    }
}

} // namespace ptarjan::detail
