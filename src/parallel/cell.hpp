//
//  cell.hpp
//  Tarjan
//
//  Created by Alex Zabrodskiy on 4/25/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

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

//A cell's status is NEW_CELL, COMPLETE_CELL, or the search whose stack holds it
inline constexpr Search* NEW_CELL = nullptr;

inline Search* completeCell() noexcept {
    alignas(std::max_align_t) static unsigned char marker; //Its address is never a Search
    return reinterpret_cast<Search*>(&marker);
}

enum class Claim { CLAIMED, OCCUPIED, COMPLETE };

} // namespace CellStatus

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
struct Cell {
private:
    //Stores searches blocked on the cell; allocated on first use
    std::atomic<SearchList*> blockedSearches{nullptr};

public:
    std::atomic<Search*> status{CellStatus::NEW_CELL};
    Vertex index{0};
    Vertex rank{0};

    //Successors are handed out in order, except that Worker::nextSuccessor may take one
    //of the next few out of turn. explored counts the successors handed out before the
    //first one that is still waiting; bit i of exploredAhead is set once successor
    //explored + 1 + i has been handed out
    std::uint32_t explored{0};
    std::uint32_t exploredAhead{0};

    Cell() = default;
    Cell(const Cell&) = delete;
    Cell& operator=(const Cell&) = delete;

    /************************************************************************************/

    bool isComplete() const { return status == CellStatus::completeCell(); }

    bool isNew() const { return status == CellStatus::NEW_CELL; }

    bool onStackOf(Search* const search) const { return search == status; }

    /* Pre: Must be the owner of the cell and all its neighbors must have been explored
     * This function effectivly retires the cell
     */
    void markComplete() { status = CellStatus::completeCell(); }

    //Returns the blocked list, or nullptr if no search has ever blocked on the cell
    SearchList* getBlockedList() const { return blockedSearches.load(); }

    void initIndex(const Vertex idx) {
        this->index = idx;
        this->rank = idx;
    }

    void promote(const Vertex rankUpdate) { this->rank = std::min(this->rank, rankUpdate); }

    /*Synchronized methods
     ***********************************************************************************/

    /** Transfer: The function changes the stack label of the cell to another search, officially transfering
     * ownership of the cell. Delta gives the cell an updated index to match the indicies of the new stack.
     * Index arithmetic is modulo 2^32, so a delta that "wraps" still lands on the right index.
     */
    void transfer(const Vertex delta, Search* const newOwner) {
        rank += delta;
        index += delta;
        status = newOwner;
    }

    //Thread safe version of the owner() method
    Search* getOwner() const {
        Search* const owner(status.load());

        if (owner == CellStatus::NEW_CELL || owner == CellStatus::completeCell())
            return nullptr;

        return owner;
    }

    //Is the cell held by a search other than this one? Used only to choose which neighbor
    //to visit next, so it may be stale by the time the caller acts on it
    bool occupiedByOther(Search* const search) const {
        Search* const owner(status.load(std::memory_order_relaxed));
        return owner != CellStatus::NEW_CELL && owner != CellStatus::completeCell() &&
               owner != search;
    }

    //Installs fresh as the cell's blocked list unless another thread installed one first;
    //either way, list is left pointing at the cell's list. Returns true if fresh was
    //installed. The CAS and the later push are sequentially consistent, so a completing
    //thread that finds no list, or a list too short to hold the suspending search, is
    //ordered before the suspender's later completeness check
    bool installBlockedList(SearchList* const fresh, SearchList*& list) {
        list = nullptr;
        if (blockedSearches.compare_exchange_strong(list, fresh)) {
            list = fresh;
            return true;
        }
        return false;
    }

    //Returns CLAIMED if and only if the search successfully took ownership of the cell
    //If conquer succeeds, the cell should be added to the conquerer's stack
    CellStatus::Claim claim(Search* const search) {
        Search* newCell(CellStatus::NEW_CELL);

        if (status.load(std::memory_order_relaxed) == CellStatus::NEW_CELL &&
            status.compare_exchange_strong(newCell, search))
            return CellStatus::Claim::CLAIMED;

        if (status.load(std::memory_order_relaxed) == CellStatus::completeCell())
            return CellStatus::Claim::COMPLETE;

        return CellStatus::Claim::OCCUPIED;
    }

    /**Attemts to conquer the cell on behalf of the search conquerer so that it can be added
     *to conquerer's stack. It is similar to the function above, but it either succeeds in
     *conquering the cell and returns true or fails and returns false and does not
     *put the search on the cell;s blockedOn list.
     */
    bool claimOrFail(Search* const conquerer) {
        Search* newCell(CellStatus::NEW_CELL);
        return status == CellStatus::NEW_CELL && status.compare_exchange_strong(newCell, conquerer);
    }
};

} // namespace ptarjan::detail

#endif
