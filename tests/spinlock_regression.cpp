#include <algorithm>
#include <atomic>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "ShardedSpinLock.h"

namespace {

struct StoredValue {
    std::uint64_t payload = 0;

    // Satisfy the legacy dictionary's unused deleteValues() interface.
    int* get() const { return nullptr; }
};

void require(bool condition, const char* message){
    if(!condition)
        throw std::runtime_error(message);
}

void checkConcurrentInserts(){
    constexpr unsigned int workerCount = 8;
    constexpr unsigned int entryCount = 4096;
    constexpr unsigned int shardStride = 2048;
    ShardedSpinLock<unsigned int, StoredValue> dictionary;
    std::atomic<unsigned int> readyWorkers{0};
    std::atomic<bool> start{false};
    std::atomic<unsigned int> successfulInserts{0};
    std::atomic<bool> incorrectResult{false};
    std::vector<std::thread> workers;

    for(unsigned int workerIndex = 0; workerIndex < workerCount; ++workerIndex){
        workers.emplace_back([&]{
            readyWorkers.fetch_add(1);
            while(!start.load())
                std::this_thread::yield();
            for(unsigned int entryIndex = 0; entryIndex < entryCount; ++entryIndex){
                // All keys use the same shard and force its unordered_map to
                // rehash while other workers contend on repeated insertions.
                const unsigned int key = entryIndex * shardStride;
                const StoredValue value{static_cast<std::uint64_t>(key) + 17};
                const auto result = dictionary.put(key, value);
                if(result.second)
                    successfulInserts.fetch_add(1);
                if(result.first.payload != value.payload || !dictionary.contains(key))
                    incorrectResult.store(true);
            }
        });
    }

    while(readyWorkers.load() != workerCount)
        std::this_thread::yield();
    start.store(true);
    for(auto& worker : workers)
        worker.join();

    require(!incorrectResult.load(), "Concurrent insert returned an incorrect value");
    require(successfulInserts.load() == entryCount, "Duplicate keys were inserted more than once");
    require(dictionary.size() == entryCount, "Concurrent insertion lost entries");

    std::unique_ptr<std::vector<unsigned int>> keys(dictionary.getKeys());
    std::sort(keys->begin(), keys->end());
    require(keys->size() == entryCount, "Key enumeration lost entries");
    for(unsigned int entryIndex = 0; entryIndex < entryCount; ++entryIndex)
        require((*keys)[entryIndex] == entryIndex * shardStride,
                "Key enumeration must restore the original key, including shard bits");
}

void checkExceptionUnlocksShard(){
    ShardedSpinLock<unsigned int, StoredValue> dictionary;
    bool lookupThrew = false;
    try {
        dictionary.get(0);
    } catch(const std::out_of_range&) {
        lookupThrew = true;
    }
    require(lookupThrew, "A missing key must throw out_of_range");
    // The old manual unlock was skipped by at() throwing, leaving this call
    // spinning forever. CTest's timeout also guards this liveness regression.
    const auto insertion = dictionary.put(2048, StoredValue{123});
    require(insertion.second && insertion.first.payload == 123,
            "A failed lookup left its shard unusable");
}

} // namespace

int main(int argumentCount, char** arguments){
    try {
        const std::string selected = argumentCount > 1 ? arguments[1] : "all";
        if(selected == "all" || selected == "concurrent")
            checkConcurrentInserts();
        if(selected == "all" || selected == "exception")
            checkExceptionUnlocksShard();
        std::cout << "Spin-lock dictionary regression checks passed\n";
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
