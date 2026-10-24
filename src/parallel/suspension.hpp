#ifndef PTARJAN_SUSPENSION_HPP
#define PTARJAN_SUSPENSION_HPP

#include <vector>

#include "cell.hpp"
#include "search.hpp"

namespace ptarjan::detail {

class Worker;

// The suspension protocol: a search that reaches a cell another search holds waits for
// that cell to become complete, and searches that wait for each other in a cycle are
// merged. The scheduler counts and queues the searches collectResumable() returns.

enum class AfterSuspend { WAIT, CONTINUE };

// Suspends search on conflictCell, which another search holds, unless the cell has
// become complete meanwhile or the suspension closes a blocking cycle that this call
// resolves. WAIT means the search is suspended and the worker must leave it.
[[nodiscard]] AfterSuspend suspend(Worker& worker, Search* const search, Cell* const conflictCell);

// Appends the searches blocked on completeCell, which has just become complete, to
// toResume, clearing their blocking cell. blocked is the cell's blocked list.
void collectResumable(const SearchList& blocked, Cell* const completeCell,
                      std::vector<Search*>& toResume);

} // namespace ptarjan::detail

#endif
