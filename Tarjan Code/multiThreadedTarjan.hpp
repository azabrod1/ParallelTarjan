//
//  multiThreadedTarjan.hpp
//  Tarjan
//
//  Created by Alex Zabrodskiy on 4/30/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#ifndef multiThreadedTarjan_hpp
#define multiThreadedTarjan_hpp

#include "typedefs.h"
#include "search.hpp"
#include "suspensionManager.hpp"
#include "pending.hpp"
#include "stealingQueue.hpp"
#include "worker.hpp"
#include "utilities.hpp"


class MultiThreadedTarjan{
    

private:
    
    //Constants
    const unsigned int NUM_THREADS;
    const uint64_t ALL_FLAGS_SET;
    
    //Data structures
    const Graph<Vid>& graph;
    Dictionary<Vid, WeakReference<Cell<Vid>>>&  dict;
    SuspensionManager susMgr;
    Pending& pending;
    StealingQueue& cellQueue;
    
    //Member Variables
    std::atomic<uint64_t> flags{0};
    
public:
    
    SCC_Set*  run();

    Search* getSearch(Worker* worker);
    

    inline bool suspend(Worker* worker, Search* const S, Cell<Vid>* conflictCell){
        return susMgr.suspend(*worker, S, conflictCell);
    }
    
    
    inline void resumeAllBlockedOn(Cell<Vid>* const completeCell, std::vector<Search*>& toResume){
        BlockedList<Search*>* suspendedList(completeCell->getBlockedList());
        
        int size(suspendedList->size());
        
        if(size){
            toResume.clear();
            susMgr.bulkUnsuspend(suspendedList, completeCell, toResume);
            
            if(!toResume.empty())
                pending.addPending(&toResume);
            
        }
        
    }
  
    
    MultiThreadedTarjan(const Graph<Vid>& _graph, Dictionary<Vid, WeakReference<Cell<Vid>>>& _dict, unsigned int num_threads , Pending& _pending, StealingQueue& _queue) : graph(_graph), dict(_dict), NUM_THREADS(num_threads), ALL_FLAGS_SET((1LL << num_threads) - 1), pending(_pending), cellQueue(_queue){
     ;
    }
   
    ~MultiThreadedTarjan();
    
    
};

#endif /* multiThreadedTarjan_hpp */

