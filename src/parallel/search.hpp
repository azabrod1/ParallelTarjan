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
    std::atomic<Cell*> cellBlockedOn{nullptr};
    TarjanStack tarjanStack;
    ControlStack controlStack; // Simulates the call stack to avoid recursion
    Vertex cellCount{0};

public:
    // Even while the search is idle; odd while a thread resolves a blocking cycle through it.
    // Grows by 2 each time the Search object is reused, so a stale view of it can be detected.
    std::atomic<std::uint32_t> age{0};

    Search() = default;
    Search(const Search&) = delete;
    Search& operator=(const Search&) = delete;

    void pushToStacks(Cell* cell);

    TarjanStack& getTS() { return tarjanStack; }

    void suspendOn(Cell* const cell) { cellBlockedOn = cell; }

    // Clears the blocking cell if it is still blockedOn. The search may have suspended on
    // another cell since, if it was resumed before the caller got here.
    [[nodiscard]] bool tryClearBlockedOn(Cell* blockedOn) {
        return cellBlockedOn.compare_exchange_strong(blockedOn, nullptr);
    }

    // Only for a search that no other thread can resume meanwhile.
    void clearBlockedOn() { cellBlockedOn = nullptr; }

    Cell* blockedOn() const { return cellBlockedOn; }

    Cell* tarjanStackTop() const { return tarjanStack.back(); }

    Cell* controlStackTop() const { return controlStack.back(); }

    void controlStackPop() { controlStack.pop_back(); }

    void tarjanStackPop() { tarjanStack.pop_back(); }

    // Prepares the object for reuse; the new age tells it apart from its earlier life.
    void reset() {
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
