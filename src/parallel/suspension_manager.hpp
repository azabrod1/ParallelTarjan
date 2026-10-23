//
//  suspension_manager.hpp
//  Tarjan
//
//  Created by Alex Zabrodskiy on 5/19/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#ifndef PTARJAN_SUSPENSION_MANAGER_HPP
#define PTARJAN_SUSPENSION_MANAGER_HPP

#include <vector>

#include "cell.hpp"
#include "search.hpp"
#include "worker.hpp"

namespace ptarjan::detail {

class SuspensionManager {

private:
    static constexpr bool SUSPEND = true;
    static constexpr bool RESUME = false;

    void runCellTransfer(Worker& worker, const std::vector<Search*>& S,
                         const std::vector<Cell*>& C);

public:
    bool suspend(Worker& worker, Search* const Sn, Cell* const conflictCell);

    void bulkUnsuspend(const SearchList* const suspendedList, Cell* const prevBlockedOn,
                       std::vector<Search*>& toResume);
};

} // namespace ptarjan::detail

#endif
