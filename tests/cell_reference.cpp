#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "Reference.hpp"

// This declaration must precede cell.h so the real Cell::getReference calls
// the controlled acquisition below. This executable deliberately does not link
// parallel_tarjan: the specialization exists in this translation unit only.
template <>
Reference<Cell<Vid>> ReferenceCounter<Cell<Vid>>::createReference();

#include "cell.h"

namespace {

std::atomic<bool> pauseNextAcquisition{false};
std::atomic<int> acquisitionPhase{0};
std::atomic<bool> forcedAcquisitionSucceeded{true};

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

template <>
Reference<Cell<Vid>> ReferenceCounter<Cell<Vid>>::createReference() {
    if (pauseNextAcquisition.exchange(false)) {
        // The caller has already checked the generation. Let the other worker
        // complete and reuse the cell before attempting the real count CAS.
        acquisitionPhase.store(1, std::memory_order_release);
        while (acquisitionPhase.load(std::memory_order_acquire) != 2) {
            std::this_thread::yield();
        }
    }

    // The orchestrated schedule leaves exactly the new owner's reference.
    // Exercise a successful acquisition against the real counter, returning a
    // real Reference with the production destructor/decrement behavior. Retry
    // logic is unnecessary because the recycler has finished before this CAS.
    int expectedCount = 1;
    if (!references.compare_exchange_strong(expectedCount, 2,
                                           std::memory_order_acquire,
                                           std::memory_order_relaxed)) {
        forcedAcquisitionSucceeded.store(false);
        return Reference<Cell<Vid>>::EmptyReference();
    }
    return Reference<Cell<Vid>>(this);
}

template <>
void ReferenceCounter<Cell<Vid>>::deleter() {
    object->recycleThis();
}

int main() {
    try {
        std::vector<Cell<Vid>*> locallyRecycledCells;
        Cell<Vid>::RECYCLED_CELLS = &locallyRecycledCells;
        Cell<Vid> cell;
        cell.initCell();
        cell.vertex = 101;
        const Vid oldGeneration = cell.age.load();

        {
            auto currentReference = cell.getReference(oldGeneration);
            require(bool(currentReference), "A live generation was unexpectedly rejected");
            require(currentReference->vertex == 101, "A live reference identified the wrong vertex");
        }
        require(cell.refCounter.getNumReferences() == 1, "A live reference was not released");

        std::atomic<bool> recyclerUsedActualPool{false};
        pauseNextAcquisition.store(true);
        std::thread recycler([&] {
            std::vector<Cell<Vid>*> recycledCells;
            Cell<Vid>::RECYCLED_CELLS = &recycledCells;
            while (acquisitionPhase.load(std::memory_order_acquire) != 1) {
                std::this_thread::yield();
            }

            // These are the production lifetime operations. Reuse the same
            // address and restore a live owner reference for a different vertex.
            cell.markComplete();
            cell.permitRecycling();
            recyclerUsedActualPool.store(recycledCells.size() == 1 &&
                                         recycledCells.back() == &cell);
            recycledCells.clear();
            cell.initCell();
            cell.vertex = 202;

            acquisitionPhase.store(2, std::memory_order_release);
            Cell<Vid>::RECYCLED_CELLS = nullptr;
        });

        auto staleReference = cell.getReference(oldGeneration);
        recycler.join();

        require(recyclerUsedActualPool.load(), "The forced schedule did not recycle through the real pool");
        require(forcedAcquisitionSucceeded.load(), "The scheduled reference-count acquisition did not occur");
        require(cell.age.load() != oldGeneration, "The forced schedule did not advance the generation");
        require(!staleReference,
                "Old-generation acquisition returned the replacement vertex after concurrent reuse");
        require(cell.refCounter.getNumReferences() == 1,
                "Rejecting the old generation did not undo its acquired reference");

        {
            auto replacementReference = cell.getReference(cell.age.load());
            require(bool(replacementReference), "The replacement generation was unexpectedly rejected");
            require(replacementReference->vertex == 202, "The replacement reference identified the wrong vertex");
        }
        require(cell.refCounter.getNumReferences() == 1, "The replacement reference was not released");
        cell.markComplete();
        cell.permitRecycling();
        require(cell.refCounter.getNumReferences() == 0, "The final owner reference was not released");
        locallyRecycledCells.clear();
        Cell<Vid>::RECYCLED_CELLS = nullptr;
        std::cout << "Cell generation-window regression passed\n";
    } catch (const std::exception& failure) {
        std::cerr << "Cell generation-window regression failed: " << failure.what() << '\n';
        return 1;
    }
}
