#ifndef PTARJAN_BLOCKED_LIST_HPP
#define PTARJAN_BLOCKED_LIST_HPP

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>

namespace ptarjan::detail {

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

 When a cell becomes complete, collectResumable() reads the entries with forEach().

 There is no delete. A search that stops waiting on the cell stays in the list, and
 collectResumable() resumes only the searches still blocked on the cell.

 N_BUFS limits the number of buffers. With the defaults the list holds more than 2^32
 entries, and each entry is a distinct suspension, so it cannot fill: a search never
 suspends twice on one cell, and there are fewer searches than vertices.

 S is the stored type, Search*. P sets the size of the first buffer and the growth
 factor.
*/
template <class S, unsigned P = 3, unsigned N_BUFS = 11>
class BlockedList {
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

    // Calls visit(entry) for each of the entries reserved so far that has been published.
    template <class Visit>
    void forEach(Visit visit) const {
        const std::size_t count = size();
        for (std::size_t i = 0; i < std::min(count, BASE); ++i)
            if (const S entry = firstBuffer[i].load())
                visit(entry);
        if (count <= BASE)
            return;
        const std::atomic<Buffer>* const extra = buffers.load();
        if (!extra)
            return;
        for (unsigned buffer = 1; buffer < N_BUFS && SUMS[buffer - 1] < count; ++buffer) {
            const Buffer entries = extra[buffer].load();
            if (!entries)
                continue;
            const std::size_t used = std::min(count, SUMS[buffer]) - SUMS[buffer - 1];
            for (std::size_t i = 0; i < used; ++i)
                if (const S entry = entries[i].load())
                    visit(entry);
        }
    }
};

} // namespace ptarjan::detail

#endif
