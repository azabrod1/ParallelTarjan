#ifndef PTARJAN_SUSPENSION_MANAGER_HPP
#define PTARJAN_SUSPENSION_MANAGER_HPP

#include <vector>

#include "cell.hpp"
#include "search.hpp"
#include "worker.hpp"

namespace ptarjan::detail {

class SuspensionManager {
    static constexpr bool SUSPEND = true;
    static constexpr bool RESUME = false;

    void runCellTransfer(Worker& worker, const std::vector<Search*>& S,
                         const std::vector<Cell*>& C);

public:
    [[nodiscard]] bool suspend(Worker& worker, Search* const Sn, Cell* const conflictCell);

    void bulkUnsuspend(const SearchList* const blockedList, Cell* const completeCell,
                       std::vector<Search*>& toResume);
};

} // namespace ptarjan::detail

#endif
