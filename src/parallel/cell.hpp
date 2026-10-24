#ifndef PTARJAN_CELL_HPP
#define PTARJAN_CELL_HPP

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "blocked_list.hpp"
#include "ptarjan/graph.hpp"

namespace ptarjan::detail {

class Search;

using SearchList = BlockedList<Search*>;

namespace CellStatus {

// A cell's status is NEW_CELL, completeCell(), or the search whose stack holds it.
inline constexpr Search* NEW_CELL = nullptr;

inline Search* completeCell() noexcept {
    alignas(std::max_align_t) static unsigned char marker; // Its address is never a Search
    return reinterpret_cast<Search*>(&marker);
}

} // namespace CellStatus

enum class Claim { CLAIMED, OCCUPIED, COMPLETE };

/**
 * The per-vertex state of the parallel search. There is one cell for every vertex,
 * allocated together before the search starts, so a cell is never reused for a
 * different vertex. (The dissertation version kept cells in a concurrent hash table
 * and recycled them with generation counts and reference counting; dense vertex
 * identifiers make all of that unnecessary.)
 *
 * status and the blocked list are shared. The other fields belong to the search
 * that owns the cell; ownership passes between searches only through claim() and
 * transfer(), and a search's owner-only state is published to the next worker by
 * the sequentially consistent operations that suspend and resume it.
 *
 * A cell does not know its vertex: that is its position in the cell array. Cells are
 * trivially destructible, so the array is freed without visiting every cell; the
 * worker that allocates a blocked list owns it.
 */
class Cell {
    // The searches blocked on the cell; allocated on first use.
    std::atomic<SearchList*> blockedSearches{nullptr};

public:
    std::atomic<Search*> status{CellStatus::NEW_CELL};
    Vertex index{0};
    Vertex rank{0};

    // Successors are handed out in order, except that Worker::nextSuccessor may take one
    // of the next few out of turn. explored counts the successors handed out before the
    // first one that is still waiting; bit i of exploredAhead is set once successor
    // explored + 1 + i has been handed out.
    std::uint32_t explored{0};
    std::uint32_t exploredAhead{0};

    Cell() = default;
    Cell(const Cell&) = delete;
    Cell& operator=(const Cell&) = delete;

    bool isComplete() const { return status == CellStatus::completeCell(); }

    bool isNew() const { return status == CellStatus::NEW_CELL; }

    // Owner only, once every successor has been explored: retires the cell.
    void markComplete() { status = CellStatus::completeCell(); }

    // The blocked list, or nullptr if no search has ever blocked on the cell.
    SearchList* blockedList() const { return blockedSearches.load(); }

    void initIndex(const Vertex idx) {
        index = idx;
        rank = idx;
    }

    void promote(const Vertex rankUpdate) { rank = std::min(rank, rankUpdate); }

    // Hands the cell to another search during a cycle resolution. delta shifts the index
    // and rank into the new stack's numbering; the arithmetic is modulo 2^32, so a delta
    // that wraps still lands on the right index.
    void transfer(const Vertex delta, Search* const newOwner) {
        rank += delta;
        index += delta;
        status = newOwner;
    }

    // The search whose stack holds the cell, or nullptr if the cell is new or complete.
    Search* owner() const {
        Search* const current = status.load();
        if (current == CellStatus::NEW_CELL || current == CellStatus::completeCell())
            return nullptr;
        return current;
    }

    // Is the cell held by a search other than this one? Used only to choose which
    // neighbor to visit next, so it may be stale by the time the caller acts on it.
    bool occupiedByOther(Search* const search) const {
        Search* const current = status.load(std::memory_order_relaxed);
        return current != CellStatus::NEW_CELL && current != CellStatus::completeCell() &&
               current != search;
    }

    // Installs fresh as the cell's blocked list unless another thread installed one first;
    // either way, list is left pointing at the cell's list. Returns true if fresh was
    // installed. The CAS and the later push are sequentially consistent, so a completing
    // thread that finds no list, or a list too short to hold the suspending search, is
    // ordered before the suspender's later completeness check.
    bool installBlockedList(SearchList* const fresh, SearchList*& list) {
        list = nullptr;
        if (blockedSearches.compare_exchange_strong(list, fresh)) {
            list = fresh;
            return true;
        }
        return false;
    }

    // Takes the cell for the search if it is new. Otherwise says whether another search
    // holds it or it is complete. A CLAIMED cell goes on the claimer's stacks.
    [[nodiscard]] Claim claim(Search* const search) {
        Search* expected = CellStatus::NEW_CELL;
        if (status.load(std::memory_order_relaxed) == CellStatus::NEW_CELL &&
            status.compare_exchange_strong(expected, search))
            return Claim::CLAIMED;
        if (status.load(std::memory_order_relaxed) == CellStatus::completeCell())
            return Claim::COMPLETE;
        return Claim::OCCUPIED;
    }

    // Takes the cell for the search if it is new; true on success.
    [[nodiscard]] bool tryClaim(Search* const search) {
        Search* expected = CellStatus::NEW_CELL;
        return status == CellStatus::NEW_CELL && status.compare_exchange_strong(expected, search);
    }
};

} // namespace ptarjan::detail

#endif
