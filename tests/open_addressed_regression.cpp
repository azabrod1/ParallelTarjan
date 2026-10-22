#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "WeakReference.h"
#include "openAddressed.h"

int main() {
    constexpr unsigned workerCount = 32;
    constexpr unsigned entriesPerWorker = 64;
    constexpr unsigned entryCount = workerCount * entriesPerWorker;
    for (unsigned round = 0; round < 20; ++round) {
        OpenAddressedMap dictionary(2); // Four slots; many concurrent writers.
        std::atomic<unsigned> readyWorkers{0};
        std::atomic<bool> start{false};
        std::atomic<bool> incorrectResult{false};
        std::vector<std::thread> workers;
        for (unsigned workerNumber = 0; workerNumber < workerCount; ++workerNumber) {
            workers.emplace_back([&, workerNumber] {
                readyWorkers.fetch_add(1);
                while (!start.load()) {
                    std::this_thread::yield();
                }
                for (unsigned entryNumber = 0; entryNumber < entriesPerWorker; ++entryNumber) {
                    const Vid key = entryNumber * workerCount + workerNumber;
                    // The dictionary only stores pointers; these unique opaque
                    // tokens are never dereferenced or owned by the map.
                    const auto pointer = reinterpret_cast<Cell<Vid>*>(static_cast<std::uintptr_t>(key + 1));
                    const WeakReference<Cell<Vid>> value(pointer, key + 7);
                    auto inserted = dictionary.put(key, value);
                    auto repeated = dictionary.put(key, value);
                    if (!inserted.second || repeated.second || inserted.first != value || repeated.first != value) {
                        incorrectResult.store(true);
                    }
                }
            });
        }
        while (readyWorkers.load() != workerCount) {
            std::this_thread::yield();
        }
        start.store(true);
        for (auto& worker : workers) {
            worker.join();
        }
        if (incorrectResult.load()) {
            std::cerr << "FAIL: concurrent inserts lost or replaced dictionary entries\n";
            return 1;
        }
        for (Vid key = 0; key < entryCount; ++key) {
            const auto pointer = reinterpret_cast<Cell<Vid>*>(static_cast<std::uintptr_t>(key + 1));
            const auto result = dictionary.put(key, WeakReference<Cell<Vid>>(pointer, key + 7));
            if (result.second || result.first.get() != pointer || result.first.age != key + 7) {
                std::cerr << "FAIL: resize lost an entry\n";
                return 1;
            }
        }
    }
    std::cout << "PASS open-addressed\n";
}
