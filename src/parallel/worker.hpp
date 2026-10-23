//
//  worker.hpp
//  Tarjan
//
//  Created by Alex Zabrodskiy on 5/10/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#ifndef PTARJAN_WORKER_HPP
#define PTARJAN_WORKER_HPP

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include "cell.hpp"
#include "root_queue.hpp"
#include "search.hpp"

namespace ptarjan::detail {

class ParallelTarjan;

//The state of one worker thread. Workers are created before any thread starts and
//destroyed after every thread has been joined, so other workers may hold pointers
//to the searches a worker allocated for the whole run
class Worker {

private:
    //Variables
    ParallelTarjan& scheduler;
    Cell* const cells;
    const std::size_t* const offsets; //The graph in CSR form; see ptarjan::Graph
    const Vertex* const targets;
    const unsigned ID;

    //The components this worker found: component i is sccVertices[sccEnds[i-1] .. sccEnds[i])
    std::vector<Vertex> sccVertices;
    std::vector<std::size_t> sccEnds;

    std::deque<Search>
        allocatedSearches; //Owns every search this worker created; a deque never moves them
    std::vector<Search*> recycled;
    Search* spareSearch;

    std::vector<Search*> toResume;

    //The blocked lists this worker installed, and one allocated for the next install
    std::vector<std::unique_ptr<SearchList>> ownedLists;
    std::unique_ptr<SearchList> spareList;

    RootRange roots; //The block of vertices this worker is scanning for new roots

    //Methods
    void execute(Search* const search);

    Cell* nextSuccessor(Cell* const curr, Search* const search);

    void buildSCC(Search* const search, Cell* const head);

    void buildSingletonSCC(Search* const search, Cell* const cell);

public:
    Worker(unsigned id, ParallelTarjan& _scheduler, Cell* _cells, const std::size_t* _offsets,
           const Vertex* _targets);

    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    void operator()();

    void reclaim(Search* const search) {
        search->reset();
        recycled.push_back(search);
    }

    unsigned getID() const { return ID; }

    Cell* cell(Vertex vertex) const { return cells + vertex; }

    Vertex vertexOf(const Cell* const cell) const { return static_cast<Vertex>(cell - cells); }

    //The cell's blocked list, installing one if no search has blocked on the cell before
    SearchList* blockedListOf(Cell* const cell);

    Search* getSpareSearch() const { return spareSearch; }

    RootRange& rootRange() { return roots; }

    //Returns the next cell of the worker's root block that is still new, or nullptr
    //once the block is used up
    Cell* nextLocalRoot() {
        while (roots.next < roots.end) {
            Cell* const candidate = cell(static_cast<Vertex>(roots.next++));
            if (candidate->isNew())
                return candidate;
        }
        return nullptr;
    }

    bool holdsRoots() const { return roots.next < roots.end; }

    void allocateSpareSearch();

    //Appends this worker's components to a result
    void collectSCCs(std::vector<Vertex>& vertices,
                     std::vector<std::size_t>& componentOffsets) const;

    //Vectors used for cycle detection protocal. Instead of allocating these heap objects each time
    //we call suspend(), each thread reuses these memory buffers
    std::vector<Search*> S;
    std::vector<Cell*> C;
    std::vector<std::uint32_t> L;

    void cleanPaths() {
        S.clear();
        C.clear();
        L.clear();
    }
};

} // namespace ptarjan::detail

#endif
