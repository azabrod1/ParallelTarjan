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

} // namespace ptarjan

#endif
