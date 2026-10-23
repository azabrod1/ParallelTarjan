//
//  root_queue.hpp
//  Tarjan
//
//  Created by Alex Zabrodskiy on 5/7/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#ifndef PTARJAN_ROOT_QUEUE_HPP
#define PTARJAN_ROOT_QUEUE_HPP

#include <algorithm>
#include <atomic>
#include <cstddef>

#include "ptarjan/graph.hpp"

namespace ptarjan::detail {

class Worker;

//A block of consecutive vertices that one worker scans for roots of new searches
struct RootRange {
    std::size_t next = 0;
    std::size_t end = 0;
};

//Hands out the vertices that new searches start from
class RootQueue {
public:
    //Gives the worker the next block of vertices, or returns false once every vertex
    //has been handed out
    virtual bool nextChunk(Worker& worker, RootRange& range) = 0;

    virtual bool isEmpty() const = 0;

    virtual ~RootQueue() = default;
};

//Offers every vertex once, in order. Workers take blocks rather than single vertices
//so that they do not all increment one shared counter for every vertex
class VertexOrderRoots : public RootQueue {
private:
    std::atomic<std::size_t> index{0};
    const std::size_t ttlCells;
    const std::size_t chunkSize;

public:
    //Blocks shrink for small graphs so that every worker still gets roots
    VertexOrderRoots(std::size_t vertexCount, unsigned threadCount)
        : ttlCells(vertexCount), chunkSize(std::clamp<std::size_t>(
                                     vertexCount / (std::size_t{threadCount} * 64 + 1), 1, 256)) {}

    bool nextChunk(Worker&, RootRange& range) override {
        const std::size_t first = index.fetch_add(chunkSize);
        if (first >= ttlCells)
            return false;
        range = {first, std::min(first + chunkSize, ttlCells)};
        return true;
    }

    bool isEmpty() const override { return index.load() >= ttlCells; }
};

} // namespace ptarjan::detail

#endif
