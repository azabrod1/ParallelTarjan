//
//  worker.hpp
//  Tarjan
//
//  Created by Alex Zabrodskiy on 5/10/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#ifndef worker_hpp
#define worker_hpp

#include <stdio.h>
#include "cell.h"
#include "typedefs.h"
#include "graph.h"
#include "dictionary.h"

class MultiThreadedTarjan;

template <class V> class Cell;
//Make sure pass ref
class Worker{
    
    friend class MultiThreadedTarjan;
    
private:
    
    //Variables
    MultiThreadedTarjan& scheduler;
    Dictionary<Vid, WeakReference<Cell<Vid>>>& dict;
    const unsigned int ID;
    const long MASK;
    std::vector<SCC*>  SCCs;
    SimpleClock sc;
    Search* spareSearch;
    std::vector<Search*> recycled;
    std::vector<Cell<Vid>*> recycledCells;
    
    
    //Methods
    void execute(Search* const  search);
    
    void initNeighbors(Cell<Vid>* cell);
        
    void buildSCC(Search* const, Cell<Vid>* const);
    
    void buildSingletonSCC(Search* const search, Cell<Vid>* const cell);
    
    void cleanUp();
    
    void allocateSpareSearch();
    
    void initNeighbors2(Cell<Vid>* cell);
    
    
    
public:
    
    const Graph<Vid>& graph;

    
    Worker(unsigned int _ID, MultiThreadedTarjan& _algo, const Graph<Vid>& _graph, Dictionary<Vid, WeakReference<Cell<Vid>>>& _dict);
    void operator()();
    
    inline void cleanPaths(){
        S.clear(); C.clear(); L.clear(); A.clear();
    }
    
    void reclaim(Search* const search){
        search->reset();
        recycled.push_back(search);
    }
    
    inline unsigned getID(){
        return ID;
    }    
    void allocateSpareCell();
    
    void buildSoloSCC(Cell<Vid>* const cell);
    
    Cell<Vid>* spareCell;
    
    //Vectors used for cycle detection protocal. Instead of allocating these heap objects each time
    //we call suspend(), each thread reuses these memory buffers
   
    std::vector<Search*> S;
    std::vector<Cell<Vid>*> C;
    std::vector<Vid> L;
    std::vector<Vid> A;
    
    //Used in an optional method, not important
    std::vector<bool> vAdded;
    
    
    
    
};




#endif /* worker_hpp */
