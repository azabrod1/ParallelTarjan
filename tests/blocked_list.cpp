#include <algorithm>
#include <atomic>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <vector>

#include "blocked_list.hpp"

namespace {

using ptarjan::detail::BlockedList;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::unordered_set<int*> collectPublishedEntries(BlockedList<int*>& blockedList) {
    std::unordered_set<int*> observed;
    // Reservations can precede publication during concurrent appends; forEach skips them.
    blockedList.forEach([&](int* entry) {
        require(entry != nullptr, "forEach visited an unpublished entry");
        require(observed.insert(entry).second, "A published entry appeared twice");
    });
    return observed;
}

void checkGrowthBoundary(std::size_t entryCount) {
    std::vector<int> payloads(entryCount);
    std::unordered_set<int*> expected;
    for (int& payload : payloads) {
        expected.insert(&payload);
    }

    for (int repetition = 0; repetition < 3; ++repetition) {
        BlockedList<int*> blockedList;
        std::atomic<std::size_t> nextEntry{0};
        std::atomic<bool> start{false};
        std::atomic<std::size_t> finishedProducers{0};
        const std::size_t producerCount = std::min<std::size_t>(32, entryCount);
        std::vector<std::thread> producers;
        producers.reserve(producerCount);
        for (std::size_t producerIndex = 0; producerIndex < producerCount; ++producerIndex) {
            producers.emplace_back([&] {
                while (!start.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
                for (;;) {
                    const std::size_t entryIndex = nextEntry.fetch_add(1);
                    if (entryIndex >= entryCount) {
                        break;
                    }
                    blockedList.push_back(&payloads[entryIndex]);
                }
                finishedProducers.fetch_add(1, std::memory_order_release);
            });
        }

        start.store(true, std::memory_order_release);
        bool snapshotsValid = true;
        while (finishedProducers.load(std::memory_order_acquire) != producerCount) {
            const auto observed = collectPublishedEntries(blockedList);
            for (int* entry : observed) {
                snapshotsValid = snapshotsValid && expected.count(entry) == 1;
            }
            std::this_thread::yield();
        }
        for (std::thread& producer : producers) {
            producer.join();
        }
        require(snapshotsValid, "A concurrent iterator returned an invalid pointer");
        require(blockedList.size() == entryCount, "Reserved entry count is wrong");
        require(collectPublishedEntries(blockedList) == expected,
                "Entries were lost while concurrently growing the blocked list");
    }
}

} // namespace

int main() {
    try {
        // The default blocks hold 8, 64, 512, 4096, ... entries. Probe both
        // sides of each cumulative boundary, including the exact switch from
        // the embedded array to the extra buffers.
        const std::vector<std::size_t> entryCounts{7,   8,   9,   71,   72,   73,
                                                   583, 584, 585, 4679, 4680, 4681};
        for (const std::size_t entryCount : entryCounts) {
            checkGrowthBoundary(entryCount);
        }
        std::cout << "Blocked-list growth and concurrent snapshot checks passed\n";
    } catch (const std::exception& failure) {
        std::cerr << "Blocked-list regression failed: " << failure.what() << '\n';
        return 1;
    }
}
