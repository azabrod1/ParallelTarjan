//
//  tarjan.hpp
//  Tarjan
//
//  Created by Alex Zabrodskiy on 4/27/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#ifndef tarjan_hpp
#define tarjan_hpp

#include <stdio.h>
#include <unordered_set>
#include "typedefs.h"
#include "graph.h"
#include "singleThreadedTarjan.h"
#include "multiThreadedTarjan.hpp"
//#include "tbb_concurrent_map.h"
#include "SimpleSharded.h"
#include "ShardedSpinLock.h"
#include "dictionaryFactory.h"

class Tarjan{
    
public:
    
    static SCC_Set* singleThreadedTarjan(const Graph<Vid>& _graph){
        SingleThreadedTarjan algorithm(_graph);
        return algorithm.run();
    }
    
    static SCC_Set* multiThreadedTarjan(const Graph<Vid>& _graph, Vid num_threads = 4, DictType dType =  OpenSharded
                    ){
        
        Dictionary<Vid, WeakReference<Cell<Vid>>>* dict = DictionaryFactory::getDictionary<Vid, WeakReference<Cell<Vid>>>(dType);
        
        LockFreePendingQueue pending;
        
        Vid numVerts; const Vid* vertices = _graph.getVerticesArray(numVerts);
        
        UnrootedStealingQueue freeCells(vertices, numVerts, *dict, num_threads);
        
        MultiThreadedTarjan algorithm(_graph, *dict, num_threads, pending, freeCells);
        
        SCC_Set* toReturn = algorithm.run();
        
        delete dict;
        
        return toReturn;
    }

    
};


#endif /* tarjan_hpp */
