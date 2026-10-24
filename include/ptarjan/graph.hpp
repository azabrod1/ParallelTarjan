#ifndef PTARJAN_GRAPH_HPP
#define PTARJAN_GRAPH_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ptarjan {

/// Vertices are numbered 0 .. vertexCount - 1.
using Vertex = std::uint32_t;

struct Edge {
    Vertex from;
    Vertex to;
};

/**
 * An immutable directed graph in compressed sparse row form: the successors of
 * vertex v are targets()[offsets()[v] .. offsets()[v + 1]).
 *
 * Edges may repeat and may be self-loops. Applications whose vertex identifiers
 * are not dense should number them 0 .. n - 1 before building the graph.
 */
class Graph {
public:
    /// The largest supported vertex count. Two identifiers are reserved as markers.
    static constexpr Vertex MAX_VERTICES = UINT32_MAX - 1;

    Graph() = default;

    /// Builds a graph from an edge list. Throws std::out_of_range if an edge names
    /// a vertex >= vertexCount, and std::length_error if vertexCount > MAX_VERTICES.
    Graph(Vertex vertexCount, std::span<const Edge> edges);

    /// Builds a graph from adjacency lists: adjacency[v] holds the successors of v.
    explicit Graph(const std::vector<std::vector<Vertex>>& adjacency);

    Vertex vertexCount() const noexcept { return static_cast<Vertex>(offsets_.size() - 1); }

    std::size_t edgeCount() const noexcept { return targets_.size(); }

    std::span<const Vertex> successors(Vertex vertex) const {
        return {targets_.data() + offsets_[vertex], targets_.data() + offsets_[vertex + 1]};
    }

    std::span<const std::size_t> offsets() const noexcept { return offsets_; }

    std::span<const Vertex> targets() const noexcept { return targets_; }

private:
    std::vector<std::size_t> offsets_{0};
    std::vector<Vertex> targets_;
};

} // namespace ptarjan

#endif
