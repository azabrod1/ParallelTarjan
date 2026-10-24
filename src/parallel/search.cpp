#include "search.hpp"

namespace ptarjan::detail {

// Defined here rather than in the header on purpose: inlined into Worker::execute, it made
// one-thread runs on DAG and clustered graphs 2-3% slower.
void Search::push(Cell* cell) {
    tarjanStack.push_back(cell);
    controlStack.push_back(cell);
    cell->initIndex(cellCount++);
}

} // namespace ptarjan::detail
