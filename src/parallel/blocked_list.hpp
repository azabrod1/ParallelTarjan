//
//  blocked_list.hpp
//  Tarjan
//
//  Created by Alex Zabrodskiy on 5/28/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#ifndef PTARJAN_BLOCKED_LIST_HPP
#define PTARJAN_BLOCKED_LIST_HPP

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>

namespace ptarjan::detail {

template <class S, unsigned P = 3, unsigned N_BUFS = 11>
class BlockedListIt;

/**The BlockedList provides a lock-free data structure to store the searches blocked on each cell

  The data structure has an initial capacity of 2^P. The idea is to choose a P so that the blockedList
  rarely exceeds size 2^P and then optimize the list so that writing and reading these first 2^P elements is
  is very fast.

  The initial buffer, firstBuffer, is a member variable of the class. This allows us to avoid pointer ndirection
  and extra mallocs by having the buffer's memory allocated as part of BlockedList

  Should the size of the list exceed capacity, blockedList allocates a new buffer of size
  sizeOfLastBuffer*(2^P). The elements in the old buffers are not moved, we simply use the old buffers
  in addition to the new ones. We hold a master List, 'buffers', which holds pointers to all the
  auxilary buffers that needed to be allocated. The 'buffers' array is only allocated if the firstBuffer's
  capacity is exhausted with a compare_and_exchange(null, new AtomicPointerArray).

  New buffers are also allocated with a compare_and_exchange.

  The N_BUFS variable limits the number of buffers. With the defaults the list holds
  more than 2^32 entries, and each entry is a distinct suspension, so it cannot fill:
  a search never suspends twice on one cell, and there are fewer searches than vertices.

  Items are inserted with the push_back(item) function. Items are inserted in order, the first one at index 0, second
  at index 1 and so on.

  The atomic member variable 'head' gives the index of the last element inserted. To insert a new element,
  a thread calls getAndIncrement() on head which returns the index the thread should insert the new element.

  After receiving an index, we retrive the corresponding buffer the index is associeted with an then
  convert the index into a position on the buffer. The arrays SUMS and POWERS, computed at compile time,
  speed up these conversions.

  When a cell becomes complete, it calls unsuspendAll() which iterates through its BlockedList to find
  out which searches may be blocked on the cell. When head < size of firstBuffer = BASE, all of
  the elements are in the firstBuffer. In this case, unsuspendAll() simply iterates though the entries of the firstBuffer
  If head >= BASE, we know some of the auxilary buffers contain entries as well. In that case, we use a
  blockedList iterator to read all the values in the data structure. It is possible to use the iterator
  to retrive all the values in the list even when head < BASE, but it is a good optimization to avoid the neccesary overhead
  when possible.

  The blockedList does not support a delete operation. Instead when a cell finds a search in the blockedList,
  it verifies that the cell is actually complete before unsuspending the search.
 */

//Template param S: The type stored in the list; we use Search*
//Template param P: Size of initial buffer = 2^P. Each additional buffer is sizeof(lastBufferAllocated)*2^p
//Template param N_BUFS: maximum number of buffers that can store the blockedList's elements

template <class S, unsigned P = 3, unsigned N_BUFS = 11>
class BlockedList {

    friend class BlockedListIt<S, P, N_BUFS>;

private:
    using Buffer = std::atomic<S>*;

    static constexpr std::size_t BASE = std::size_t{1} << P; // the size of the firstBuffer

    //POWERS[b] is the size of buffer b and SUMS[b] the total size of buffers 0..b
    static constexpr std::array<std::size_t, N_BUFS> POWERS = [] {
        std::array<std::size_t, N_BUFS> powers{};
        powers[0] = BASE;
        for (unsigned buffer = 1; buffer < N_BUFS; ++buffer)
            powers[buffer] = powers[buffer - 1] << P;
        return powers;
    }();

    static constexpr std::array<std::size_t, N_BUFS> SUMS = [] {
        std::array<std::size_t, N_BUFS> sums{};
        sums[0] = BASE;
        for (unsigned buffer = 1; buffer < N_BUFS; ++buffer)
            sums[buffer] = sums[buffer - 1] + POWERS[buffer];
        return sums;
    }();

    std::atomic<std::size_t> head{0};

    std::atomic<S> firstBuffer[BASE]{};

    //Atomic pointer to array of extra buffers, which are allocated if needed
    std::atomic<std::atomic<Buffer>*> buffers{nullptr};

    //Returns the buffer associated with the index 'num'
    static unsigned findBuffer(const std::size_t num) {
        unsigned loc{0};
        while (num >= SUMS[loc])
            if (++loc == N_BUFS)
                std::terminate(); //More entries than suspensions can produce; see above
        return loc;
    }

public:
    BlockedList() = default;
    BlockedList(const BlockedList&) = delete;
    BlockedList& operator=(const BlockedList&) = delete;

    //Deletes the buffers used to store elements in blockedList
    ~BlockedList() {
        if (std::atomic<Buffer>* const extra = buffers.load()) {
            for (unsigned arr = 1; arr < N_BUFS;
                 ++arr) //1st buf not dynamically allocated; dont delete
                delete[] extra[arr].load();
            delete[] extra;
        }
    }

    /* Function: push_back()
     The push_back() function inserts @item to the back of the list.
     Head is incremented atomically and its previous value idx is retrieived.
     The function will store item at the location represented by idx.
     If idx < BASE, the item is inserted into firstBuffer[idx] and the function returns.

     Otherwise, findBuffer() is used to calculate the auxilary buffer and position
     idx corresponds to. Before inserting item into the buffer, the function verifies that the
     buffer actually exists. If the buffer does not exist, we allocate
     space for the buffer and use a compare_and_exchange() call to set the buffer to the one we created
     Afterward, the correct item is inserted into the correct location in the buffer.
     */
    void push_back(const S& item) {
        const std::size_t idx(head++);

        //if index is less than base, we just insert the search in the correct location of firstBuffer
        if (idx < BASE) {
            // Publication participates in the same sequentially consistent
            // order as suspension and cell completion. A relaxed store can
            // otherwise be missed by the completing thread even after the
            // suspending thread has observed an incomplete cell.
            firstBuffer[idx].store(item);
            return;
        }

        //calculate the auxilary buffer and the index of the new buffer we must insert into
        const unsigned buffer(findBuffer(idx));
        const std::size_t pos(idx - SUMS[buffer - 1]);

        //Initialize the array of auxilary buffers if it has not yet been initilized
        //We also initialize the first auxilary buffer right away to avoid doing two compare_and_exchanges
        //since we would have had to do it anyway
        std::atomic<Buffer>* extra = buffers.load();
        if (!extra) {
            auto* const bufferArray = new std::atomic<Buffer>[N_BUFS]();
            bufferArray[0] = firstBuffer;
            bufferArray[1] = new std::atomic<S>[POWERS[1]]();

            if (buffers.compare_exchange_strong(extra, bufferArray))
                extra = bufferArray;
            else { //If another thread added the auxilary buffers array first, we delete the one we created
                delete[] bufferArray[1].load();
                delete[] bufferArray;
            }
        }

        //Allocate the buffer we plan to insert into if it has not yet been created
        Buffer target = extra[buffer].load();
        if (!target) {
            auto* const newBuffer(new std::atomic<S>[POWERS[buffer]]());

            //If another thread has already added a buffer, we delete the one we created
            if (extra[buffer].compare_exchange_strong(target, newBuffer))
                target = newBuffer;
            else
                delete[] newBuffer;
        }

        target[pos].store(item); //Publish in the suspension/completion order.
    }

    std::size_t size() const { return head.load(); }

    //Is the list small enough that we can use the more efficient iterator to read its elements
    static bool isSmallList(const std::size_t size) { return size < BASE; }

    const std::atomic<S>* getSmallBuffer() const { return firstBuffer; }

    //get iterator to the blockedList
    BlockedListIt<S, P, N_BUFS> getIt(const std::size_t size) const {
        return BlockedListIt<S, P, N_BUFS>(*this, size);
    }
};

/* Iterator for the blockedList, used when the blockedList has more than 2^P entries.
 * Entries that are reserved but not yet published read as null. */
template <class S, unsigned P, unsigned N_BUFS>
class BlockedListIt {

private:
    using List = BlockedList<S, P, N_BUFS>;

    //Both counters start one before the first entry; the first increment wraps them to 0
    std::size_t idx{SIZE_MAX}, ttl{SIZE_MAX};
    unsigned arr{0};
    const std::size_t last;
    const List& list;

public:
    BlockedListIt(const List& _list, std::size_t _last) : last{_last}, list(_list) {}

    S getNext() const {
        if (!arr)
            return list.firstBuffer[idx];

        std::atomic<typename List::Buffer>* const extra = list.buffers.load();
        if (!extra)
            return nullptr;
        std::atomic<S>* const buffer = extra[arr].load();
        return buffer ? buffer[idx].load() : nullptr;
    }

    //Increments the iterator, returning true if the list has more elements to consume
    bool increment() {
        if (++ttl == last)
            return false;

        if (ttl == List::SUMS[arr]) {
            ++arr;
            idx = 0;
        } else
            ++idx;

        return true;
    }
};

} // namespace ptarjan::detail

#endif
