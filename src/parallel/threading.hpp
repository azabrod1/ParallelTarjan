#ifndef PTARJAN_THREADING_HPP
#define PTARJAN_THREADING_HPP

#include <atomic>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace ptarjan::detail {

// Throws std::invalid_argument unless there is at least one worker thread.
inline unsigned checkedThreadCount(unsigned threadCount, const char* algorithm) {
    if (threadCount == 0)
        throw std::invalid_argument(std::string(algorithm) + " needs at least one worker thread");
    return threadCount;
}

// The first exception thrown on any of an algorithm's threads (in practice,
// std::bad_alloc). Once one is recorded, aborted() tells the other threads to stop, and
// the caller rethrows it after joining them.
class FirstFailure {
public:
    void record(std::exception_ptr error) {
        {
            const std::scoped_lock lock(mutex);
            if (!failure)
                failure = std::move(error);
        }
        stop = true;
    }

    bool aborted(std::memory_order order = std::memory_order_seq_cst) const {
        return stop.load(order);
    }

    // Only once every thread that may record a failure has been joined.
    void rethrowIfAny() const {
        if (failure)
            std::rethrow_exception(failure);
    }

private:
    std::atomic<bool> stop{false};
    std::mutex mutex;
    std::exception_ptr failure;
};

} // namespace ptarjan::detail

#endif
