#include "ptarjan/graph.hpp"

#include <limits>
#include <stdexcept>
#include <string>

namespace ptarjan {

namespace {

void checkVertexCount(std::size_t vertexCount) {
    if (vertexCount > Graph::MAX_VERTICES)
        throw std::length_error("ptarjan::Graph supports at most " +
                                std::to_string(Graph::MAX_VERTICES) + " vertices");
}

void checkVertex(Vertex vertex, std::size_t vertexCount) {
    if (vertex >= vertexCount)
        throw std::out_of_range("edge endpoint " + std::to_string(vertex) +
                                " is not a vertex of a graph with " + std::to_string(vertexCount) +
                                " vertices");
}

// The parallel search keeps a 32-bit cursor into each successor list.
void checkDegree(std::size_t degree) {
    if (degree > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("a vertex has more than 2^32 - 1 successors");
}

} // namespace

Graph::Graph(Vertex vertexCount, std::span<const Edge> edges) {
    checkVertexCount(vertexCount);

    // Counting sort by source keeps each vertex's edges in input order.
    offsets_.assign(std::size_t{vertexCount} + 1, 0);
    for (const Edge& edge : edges) {
        checkVertex(edge.from, vertexCount);
        checkVertex(edge.to, vertexCount);
        ++offsets_[edge.from + 1];
    }
    for (std::size_t vertex = 0; vertex < vertexCount; ++vertex) {
        checkDegree(offsets_[vertex + 1]);
        offsets_[vertex + 1] += offsets_[vertex];
    }

    targets_.resize(edges.size());
    std::vector<std::size_t> next(offsets_.begin(), offsets_.end() - 1);
    for (const Edge& edge : edges)
        targets_[next[edge.from]++] = edge.to;
}

Graph::Graph(const std::vector<std::vector<Vertex>>& adjacency) {
    checkVertexCount(adjacency.size());

    offsets_.reserve(adjacency.size() + 1);
    std::size_t edgeCount = 0;
    for (const auto& successors : adjacency) {
        checkDegree(successors.size());
        edgeCount += successors.size();
        offsets_.push_back(edgeCount);
    }

    targets_.reserve(edgeCount);
    for (const auto& successors : adjacency) {
        for (const Vertex successor : successors) {
            checkVertex(successor, adjacency.size());
            targets_.push_back(successor);
        }
    }
}

} // namespace ptarjan
