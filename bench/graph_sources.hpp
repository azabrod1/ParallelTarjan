#ifndef PTARJAN_BENCH_GRAPH_SOURCES_HPP
#define PTARJAN_BENCH_GRAPH_SOURCES_HPP

#include <cstdint>
#include <string>
#include <string_view>

#include "ptarjan/graph.hpp"

namespace ptarjan::bench {

struct GraphSpec {
    std::string family = "random"; // see describeFamilies()
    Vertex vertices = 1'000'000;
    double degree = 4.0;     // average out-degree
    Vertex clusterSize = 64; // for the clusters family
    std::uint64_t seed = 1;
};

/// Builds a synthetic graph, or reads one from "edges:<path>" or "csp:<path>".
/// Throws std::invalid_argument for an unknown family or an unreadable file.
Graph makeGraph(const GraphSpec& spec);

/// One line per supported family, for --help.
std::string_view describeFamilies();

} // namespace ptarjan::bench

#endif
