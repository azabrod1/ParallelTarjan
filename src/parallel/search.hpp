//
//  search.hpp
//  Tarjan
//
//  Created by Alex Zabrodskiy on 4/26/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#ifndef PTARJAN_SEARCH_HPP
#define PTARJAN_SEARCH_HPP

#include <atomic>
#include <cstdint>
#include <vector>

#include "cell.hpp"

namespace ptarjan::detail {

using TarjanStack = std::vector<Cell*>;
using ControlStack = std::vector<Cell*>;

class Worker;

class Search {

private:
    std::atomic<Cell*> cellBlockedOn{nullptr};
    TarjanStack tarjanStack;
    ControlStack controlStack; //Simulates the call stack to avoid recursion
    Vertex cellCount{0};

public:
    //Even while the search is idle; odd while a thread resolves a blocking cycle through it.
    //Grows by 2 each time the Search object is reused, so a stale view of it can be detected
    std::atomic<std::uint32_t> age{0};

    Search() = default;
    Search(const Search&) = delete;
    Search& operator=(const Search&) = delete;

    void pushToStacks(Cell* cell);

    TarjanStack& getTS() { return tarjanStack; }

    void setRoot(Cell* const root) { pushToStacks(root); }

    void suspendOn(Cell* const _cell) { cellBlockedOn = _cell; }

    /*We use a compare and exchange when the search may have become blocked on a new cell before the function call
     *This can happen if the search resumes before an unsuspend finishes */
    bool removeCellBlockedOnWithCompareExchange(Cell* blockedOn) {
        return cellBlockedOn.compare_exchange_strong(blockedOn, nullptr);
    }

    //use only when you are sure there will not be race condition, otherwise use the one above.
    void removeCellBlockedOn() { cellBlockedOn = nullptr; }

    Cell* tarjanStackTop() const { return tarjanStack.back(); }

    Cell* controlStackTop() const { return controlStack.back(); }

    bool controlStackEmpty() const { return controlStack.empty(); }

    void controlStackPop() { controlStack.pop_back(); }

    void tarjanStackPop() { tarjanStack.pop_back(); }

    Cell* getBlockingCell() const { return cellBlockedOn; }

    void reset() {
        //Increment 'age' counter to prevent the search being confused with one
        //from an earlier lifetime
        cellCount = 0;
        age += 2;
    }

    void refreshCellCount();

    bool done() const { return controlStack.empty(); }

    static void transferCells(Worker& worker, Search* const src, Search* const dest,
                              Cell* const conflictCell);
};

} // namespace ptarjan::detail

#endif
