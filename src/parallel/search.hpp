#ifndef PTARJAN_SEARCH_HPP
#define PTARJAN_SEARCH_HPP

#include <atomic>
#include <cstdint>
#include <vector>

#include "cell.hpp"

namespace ptarjan::detail {

using TarjanStack = std::vector<Cell*>;
using ControlStack = std::vector<Cell*>;

/**
 * One depth-first search of the parallel algorithm.
 *
 * The stacks and cellCount belong to the worker running the search, or to the thread
 * resolving a blocking cycle through it, and pass to the next one through the
 * sequentially consistent operations that suspend and resume the search. The blocking
 * cell and age are read by other threads walking blocking paths.
 */
class Search {
    std::atomic<Cell*> cellBlockedOn{nullptr};

public:
    TarjanStack tarjanStack;
    ControlStack controlStack; // Simulates the call stack to avoid recursion
    Vertex cellCount{0};       // The index the next cell pushed receives

    // Even while the search is idle; odd while a thread resolves a blocking cycle through it.
    // Grows by 2 each time the Search object is reused, so a stale view of it can be detected.
    std::atomic<std::uint32_t> age{0};

    Search() = default;
    Search(const Search&) = delete;
    Search& operator=(const Search&) = delete;

    // Pre: the search owns the cell. Puts it on both stacks with the next index.
    void push(Cell* cell);

    bool done() const { return controlStack.empty(); }

    // After a transfer the stack's top may have come from another search.
    void refreshCellCount() { cellCount = tarjanStack.back()->index + 1; }

    // Prepares the object for reuse; the new age tells it apart from its earlier life.
    void reset() {
        cellCount = 0;
        age += 2;
    }

    Cell* blockedOn() const { return cellBlockedOn; }

    void suspendOn(Cell* const cell) { cellBlockedOn = cell; }

    // Clears the blocking cell if it is still blockedOn. The search may have suspended on
    // another cell since, if it was resumed before the caller got here.
    [[nodiscard]] bool tryClearBlockedOn(Cell* blockedOn) {
        return cellBlockedOn.compare_exchange_strong(blockedOn, nullptr);
    }

    // Only for a search that no other thread can resume meanwhile.
    void clearBlockedOn() { cellBlockedOn = nullptr; }
};

} // namespace ptarjan::detail

#endif
