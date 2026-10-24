#ifndef PTARJAN_PENDING_QUEUE_HPP
#define PTARJAN_PENDING_QUEUE_HPP

#include <deque>
#include <memory>
#include <mutex>
#include <new>
#include <span>
#include <vector>

#include "concurrentqueue.h" // moodycamel::ConcurrentQueue, fetched by CMake

namespace ptarjan::detail {

class Search;

// Searches that have been unsuspended and are waiting for a worker to resume them.
// Each worker passes its ID, and only that worker's thread uses that ID.
class PendingQueue {
public:
    virtual void addPending(unsigned worker, std::span<Search* const> searches) = 0;

    // Returns nullptr if no search is waiting.
    virtual Search* get(unsigned worker) = 0;

    virtual ~PendingQueue() = default;
};

class LockFreePendingQueue final : public PendingQueue {
    moodycamel::ConcurrentQueue<Search*> waiting;

    // Without tokens, every dequeue scans all the producers. Measured with 16 workers on a
    // graph with many suspensions, tokens cut the run time by about a quarter.
    struct Tokens {
        moodycamel::ProducerToken producer;
        moodycamel::ConsumerToken consumer;
        explicit Tokens(moodycamel::ConcurrentQueue<Search*>& queue)
            : producer(queue), consumer(queue) {
            // A producer token whose allocation failed is invalid, not an exception, and
            // passing it to the queue dereferences null.
            if (!producer.valid())
                throw std::bad_alloc();
        }
    };
    std::vector<std::unique_ptr<Tokens>> tokens; // Destroyed before the queue they refer to

    Tokens& tokensOf(unsigned worker) {
        if (!tokens[worker])
            tokens[worker] = std::make_unique<Tokens>(waiting);
        return *tokens[worker];
    }

public:
    explicit LockFreePendingQueue(unsigned workerCount) : tokens(workerCount) {}

    void addPending(unsigned worker, std::span<Search* const> searches) override {
        if (!waiting.enqueue_bulk(tokensOf(worker).producer, searches.data(), searches.size()))
            throw std::bad_alloc();
    }

    Search* get(unsigned worker) override {
        Search* search = nullptr;
        return waiting.try_dequeue(tokensOf(worker).consumer, search) ? search : nullptr;
    }
};

// A mutex-protected queue. ThreadSanitizer cannot model the fences the lock-free queue
// relies on, so the sanitizer tests use this one to check the rest of the algorithm.
class LockedPendingQueue final : public PendingQueue {
    std::mutex mu;
    std::deque<Search*> waiting;

public:
    void addPending(unsigned, std::span<Search* const> searches) override {
        const std::scoped_lock lock(mu);
        waiting.insert(waiting.end(), searches.begin(), searches.end());
    }

    Search* get(unsigned) override {
        const std::scoped_lock lock(mu);
        if (waiting.empty())
            return nullptr;
        Search* const search = waiting.front();
        waiting.pop_front();
        return search;
    }
};

} // namespace ptarjan::detail

#endif
