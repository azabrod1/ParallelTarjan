#ifndef PTARJAN_ROOT_QUEUE_HPP
#define PTARJAN_ROOT_QUEUE_HPP

#include <algorithm>
#include <atomic>
#include <cstddef>

#include "ptarjan/graph.hpp"

namespace ptarjan::detail {

class Worker;

// A block of consecutive vertices that one worker scans for roots of new searches.
struct RootRange {
    std::size_t next = 0;
    std::size_t end = 0;
};

// Hands out the vertices that new searches start from.
class RootQueue {
public:
    // Gives the worker the next block of vertices, or returns false once every vertex
    // has been handed out.
    [[nodiscard]] virtual bool nextBlock(Worker& worker, RootRange& range) = 0;

    virtual bool isEmpty() const = 0;

    virtual ~RootQueue() = default;
};

// Offers every vertex once, in order. Workers take blocks rather than single vertices
// so that they do not all increment one shared counter for every vertex.
class VertexOrderRoots : public RootQueue {
    std::atomic<std::size_t> nextVertex{0};
    const std::size_t vertexCount;
    const std::size_t blockSize;

public:
    // Blocks shrink for small graphs so that every worker still gets roots.
    VertexOrderRoots(std::size_t vertices, unsigned threadCount)
        : vertexCount(vertices), blockSize(std::clamp<std::size_t>(
                                     vertices / (std::size_t{threadCount} * 64 + 1), 1, 256)) {}

    bool nextBlock(Worker&, RootRange& range) override {
        const std::size_t first = nextVertex.fetch_add(blockSize);
        if (first >= vertexCount)
            return false;
        range = {first, std::min(first + blockSize, vertexCount)};
        return true;
    }

    bool isEmpty() const override { return nextVertex.load() >= vertexCount; }
};

} // namespace ptarjan::detail

#endif
