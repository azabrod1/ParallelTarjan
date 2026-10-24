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

/*
 A lock-free, append-only list of the searches blocked on one cell.

 The first 2^P entries live in firstBuffer, inside the list itself, so the common case
 of a few blocked searches needs no further allocation or indirection. Beyond that, each
 extra buffer is 2^P times the size of the one before. Entries never move: a full buffer
 stays in use and the next one is added. The array of extra buffers, `buffers`, and each
 buffer in it are allocated on first use and installed with a CAS; a thread that loses
 the race frees its copy.

 push_back() reserves the next index with a fetch_add on head, so head is the number of
 entries reserved, and findBuffer() maps the index to a buffer using the SUMS table
 computed at compile time. A reserved entry that is not yet published reads as null.

 When a cell becomes complete, SuspensionManager::bulkUnsuspend reads the entries: from
 firstBuffer directly while head < BASE, through BlockedListIt otherwise.

 There is no delete. A search that stops waiting on the cell stays in the list, and
 bulkUnsuspend resumes only the searches still blocked on the cell.

 N_BUFS limits the number of buffers. With the defaults the list holds more than 2^32
 entries, and each entry is a distinct suspension, so it cannot fill: a search never
 suspends twice on one cell, and there are fewer searches than vertices.

 S is the stored type, Search*. P sets the size of the first buffer and the growth
 factor.
*/
template <class S, unsigned P = 3, unsigned N_BUFS = 11>
class BlockedList {

    friend class BlockedListIt<S, P, N_BUFS>;

private:
    using Buffer = std::atomic<S>*;

    static constexpr std::size_t BASE = std::size_t{1} << P; // The size of firstBuffer

    // POWERS[b] is the size of buffer b and SUMS[b] the total size of buffers 0 .. b.
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

    // The extra buffers, allocated on first use. Entry 0 points at firstBuffer.
    std::atomic<std::atomic<Buffer>*> buffers{nullptr};

    // The buffer that holds entry num.
    static unsigned findBuffer(const std::size_t num) {
        unsigned loc{0};
        while (num >= SUMS[loc])
            if (++loc == N_BUFS)
                std::terminate(); // More entries than suspensions can produce; see above
        return loc;
    }

public:
    BlockedList() = default;
    BlockedList(const BlockedList&) = delete;
    BlockedList& operator=(const BlockedList&) = delete;

    ~BlockedList() {
        if (std::atomic<Buffer>* const extra = buffers.load()) {
            for (unsigned buffer = 1; buffer < N_BUFS; ++buffer) // Entry 0 is firstBuffer
                delete[] extra[buffer].load();
            delete[] extra;
        }
    }

    // Appends item, allocating the buffer that its index falls in if no thread has yet.
    void push_back(const S& item) {
        const std::size_t idx(head++);

        if (idx < BASE) {
            // Publication participates in the same sequentially consistent
            // order as suspension and cell completion. A relaxed store can
            // otherwise be missed by the completing thread even after the
            // suspending thread has observed an incomplete cell.
            firstBuffer[idx].store(item);
            return;
        }

        const unsigned buffer(findBuffer(idx));
        const std::size_t pos(idx - SUMS[buffer - 1]);

        // The first extra buffer is created with the array, since it is needed now anyway.
        std::atomic<Buffer>* extra = buffers.load();
        if (!extra) {
            auto* const bufferArray = new std::atomic<Buffer>[N_BUFS]();
            bufferArray[0] = firstBuffer;
            bufferArray[1] = new std::atomic<S>[POWERS[1]]();

            if (buffers.compare_exchange_strong(extra, bufferArray))
                extra = bufferArray;
            else { // Another thread installed the array first
                delete[] bufferArray[1].load();
                delete[] bufferArray;
            }
        }

        Buffer target = extra[buffer].load();
        if (!target) {
            auto* const newBuffer(new std::atomic<S>[POWERS[buffer]]());

            if (extra[buffer].compare_exchange_strong(target, newBuffer))
                target = newBuffer;
            else
                delete[] newBuffer; // Another thread installed it first
        }

        target[pos].store(item); // Publish in the suspension/completion order
    }

    std::size_t size() const { return head.load(); }

    // Whether every entry is in firstBuffer, which can be read without the iterator.
    static bool isSmallList(const std::size_t size) { return size < BASE; }

    const std::atomic<S>* getSmallBuffer() const { return firstBuffer; }

    // An iterator over the first size entries.
    BlockedListIt<S, P, N_BUFS> getIt(const std::size_t size) const {
        return BlockedListIt<S, P, N_BUFS>(*this, size);
    }
};

// Iterates over a BlockedList with more than 2^P entries. Entries that are reserved but
// not yet published read as null.
template <class S, unsigned P, unsigned N_BUFS>
class BlockedListIt {

private:
    using List = BlockedList<S, P, N_BUFS>;

    // Both counters start one before the first entry; the first increment wraps them to 0.
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

    // Advances to the next entry; false once the entries are used up.
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
