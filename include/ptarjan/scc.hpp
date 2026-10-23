#ifndef PTARJAN_SCC_HPP
#define PTARJAN_SCC_HPP

#include "ptarjan/components.hpp"
#include "ptarjan/graph.hpp"

namespace ptarjan {

/// Tarjan's algorithm on one thread, iteratively, so deep graphs cannot overflow
/// the call stack.
Components sequentialScc(const Graph& graph);

/// The number of worker threads parallelScc uses by default: the hardware
/// concurrency, or 1 if that is unknown.
unsigned defaultThreadCount() noexcept;

/**
 * The parallel version of Tarjan's algorithm from the dissertation: several
 * depth-first searches run at once, and a search that reaches a vertex owned by
 * another suspends until that vertex is assigned to a component. Searches that
 * block each other in a cycle are merged by transferring vertices between their
 * stacks.
 *
 * Throws std::invalid_argument if threadCount is 0. The graph must not change
 * while the call runs.
 */
Components parallelScc(const Graph& graph, unsigned threadCount = defaultThreadCount());

/**
 * A hybrid algorithm for graphs with a giant component, where parallelScc
 * cannot spread the work. Breadth-first parallel phases come first: trimming
 * removes vertices with no in- or out-edges among those remaining (repeatedly,
 * which finishes a DAG), and a forward-backward search from the vertex of
 * largest in-degree x out-degree extracts that vertex's component. The parallel
 * Tarjan search then handles the remaining vertices.
 *
 * It builds the reverse graph and per-vertex state for the breadth-first
 * phases: at its peak it needs extra memory of about twice the graph's size
 * beyond what parallelScc needs. While a breadth-first frontier is large, the
 * threads synchronize once per level; small frontiers, as on long paths, are
 * advanced by one thread. Throws std::invalid_argument if threadCount is 0.
 */
Components hybridScc(const Graph& graph, unsigned threadCount = defaultThreadCount());

} // namespace ptarjan

#endif
