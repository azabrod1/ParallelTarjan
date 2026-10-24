#include "graph_sources.hpp"

#include <algorithm>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ptarjan::bench {

namespace {

// std::mt19937_64 is fully specified, unlike the standard distributions, so a
// seed produces the same graph on every platform.
class Random {
public:
    explicit Random(std::uint64_t seed) : engine(seed) {}

    // Uniform enough in [0, bound) for benchmark graphs.
    Vertex below(std::uint64_t bound) { return static_cast<Vertex>(engine() % bound); }

private:
    std::mt19937_64 engine;
};

std::size_t edgeCountFor(const GraphSpec& spec) {
    return static_cast<std::size_t>(spec.degree * static_cast<double>(spec.vertices));
}

// G(n, m): uniformly random edges. With an average degree above 1 most vertices
// fall into one giant component.
Graph randomGraph(const GraphSpec& spec, Random& random) {
    std::vector<Edge> edges(edgeCountFor(spec));
    for (Edge& edge : edges)
        edge = {random.below(spec.vertices), random.below(spec.vertices)};
    return Graph(spec.vertices, edges);
}

// Clusters of clusterSize vertices with random edges inside each cluster (one
// SCC per cluster, in practice) and a few edges from each cluster to later
// clusters, so the clusters form a DAG. Many medium components and frequent
// collisions between searches.
Graph clusterGraph(const GraphSpec& spec, Random& random) {
    const Vertex size = std::max<Vertex>(1, spec.clusterSize);
    std::vector<Edge> edges;
    edges.reserve(edgeCountFor(spec) + spec.vertices);
    for (Vertex vertex = 0; vertex < spec.vertices; ++vertex) {
        const Vertex clusterStart = vertex - vertex % size;
        const Vertex clusterEnd = std::min<Vertex>(clusterStart + size, spec.vertices);
        // A cycle through the cluster keeps it strongly connected.
        edges.push_back({vertex, vertex + 1 == clusterEnd ? clusterStart : vertex + 1});
        for (int edge = 1; edge < static_cast<int>(spec.degree); ++edge)
            edges.push_back({vertex, clusterStart + random.below(clusterEnd - clusterStart)});
        if (clusterEnd < spec.vertices && random.below(4) == 0)
            edges.push_back({vertex, clusterEnd + random.below(spec.vertices - clusterEnd)});
    }
    return Graph(spec.vertices, edges);
}

// Edges only to later vertices within a window: every component is a single vertex.
Graph dagGraph(const GraphSpec& spec, Random& random) {
    std::vector<Edge> edges;
    edges.reserve(edgeCountFor(spec));
    for (Vertex vertex = 0; vertex + 1 < spec.vertices; ++vertex) {
        const Vertex window = std::min<Vertex>(spec.vertices - vertex - 1, 1000);
        for (int edge = 0; edge < static_cast<int>(spec.degree); ++edge)
            edges.push_back({vertex, vertex + 1 + random.below(window)});
    }
    return Graph(spec.vertices, edges);
}

Graph chainGraph(const GraphSpec& spec, bool closeCycle) {
    std::vector<Edge> edges;
    edges.reserve(spec.vertices);
    for (Vertex vertex = 0; vertex + 1 < spec.vertices; ++vertex)
        edges.push_back({vertex, vertex + 1});
    if (closeCycle && spec.vertices > 0)
        edges.push_back({spec.vertices - 1, 0});
    return Graph(spec.vertices, edges);
}

using LabeledEdge = std::pair<std::uint64_t, std::uint64_t>;

// Numbers the labels 0 .. n - 1 in increasing order, so a file that already uses
// 0 .. n - 1 keeps its numbering (and the locality that numbering may carry).
// labels holds vertices that need not appear in any edge.
Graph denseGraph(const std::vector<LabeledEdge>& labeledEdges, std::vector<std::uint64_t> labels) {
    labels.reserve(labels.size() + 2 * labeledEdges.size());
    for (const auto& [from, to] : labeledEdges) {
        labels.push_back(from);
        labels.push_back(to);
    }
    std::sort(labels.begin(), labels.end());
    labels.erase(std::unique(labels.begin(), labels.end()), labels.end());
    if (labels.size() > Graph::MAX_VERTICES)
        throw std::length_error("graph file has too many vertices");

    auto number = [&](std::uint64_t label) {
        return static_cast<Vertex>(std::lower_bound(labels.begin(), labels.end(), label) -
                                   labels.begin());
    };
    std::vector<Edge> edges;
    edges.reserve(labeledEdges.size());
    for (const auto& [from, to] : labeledEdges)
        edges.push_back({number(from), number(to)});
    return Graph(static_cast<Vertex>(labels.size()), edges);
}

std::ifstream openFile(const std::string& path) {
    std::ifstream file(path);
    if (!file)
        throw std::invalid_argument("cannot read graph file: " + path);
    return file;
}

// One edge per line, "from to", as in the SNAP and KONECT collections. Lines
// starting with '#' or '%' are comments.
Graph readEdgeList(const std::string& path) {
    std::ifstream file = openFile(path);
    std::vector<LabeledEdge> edges;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#' || line[0] == '%')
            continue;
        std::istringstream fields(line);
        std::uint64_t from = 0, to = 0;
        if (!(fields >> from >> to))
            throw std::invalid_argument("bad edge line in " + path + ": " + line);
        edges.emplace_back(from, to);
    }
    return denseGraph(edges, {});
}

// The state-space format used for the dissertation's benchmarks: a header line,
// then "vertex: successor successor ..." per line.
Graph readCspGraph(const std::string& path) {
    std::ifstream file = openFile(path);
    std::vector<LabeledEdge> edges;
    std::vector<std::uint64_t> vertices;
    std::string line;
    std::getline(file, line); // header
    while (std::getline(file, line)) {
        if (line.find_first_not_of(" \t\r") == std::string::npos)
            continue;
        std::istringstream fields(line);
        std::uint64_t vertex = 0, successor = 0;
        char colon = 0;
        if (!(fields >> vertex >> colon) || colon != ':')
            throw std::invalid_argument("bad CSP graph line in " + path + ": " + line);
        vertices.push_back(vertex);
        while (fields >> successor)
            edges.emplace_back(vertex, successor);
    }
    return denseGraph(edges, std::move(vertices));
}

} // namespace

Graph makeGraph(const GraphSpec& spec) {
    const std::string& family = spec.family;
    if (family.starts_with("edges:"))
        return readEdgeList(family.substr(6));
    if (family.starts_with("csp:"))
        return readCspGraph(family.substr(4));

    Random random(spec.seed);
    if (family == "random")
        return randomGraph(spec, random);
    if (family == "clusters")
        return clusterGraph(spec, random);
    if (family == "dag")
        return dagGraph(spec, random);
    if (family == "chain")
        return chainGraph(spec, false);
    if (family == "cycle")
        return chainGraph(spec, true);
    throw std::invalid_argument("unknown graph family: " + family);
}

std::string_view describeFamilies() {
    return "  random       uniformly random edges (a giant component once degree > 1)\n"
           "  clusters     strongly connected clusters of --cluster-size, linked as a DAG\n"
           "  dag          edges only to nearby later vertices (every component is one vertex)\n"
           "  chain        a path 0 -> 1 -> ... -> n-1\n"
           "  cycle        a single cycle through every vertex\n"
           "  edges:FILE   an edge list, one \"from to\" pair per line (SNAP format)\n"
           "  csp:FILE     the dissertation's \"vertex: successors...\" state-space format\n";
}

} // namespace ptarjan::bench
