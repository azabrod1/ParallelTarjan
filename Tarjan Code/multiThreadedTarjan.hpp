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
#include <cstdint>
#include <limits>
#include <stdexcept>


class MultiThreadedTarjan{
    

private:
    
    //Constants
    const unsigned int NUM_THREADS;
    const std::uint64_t ALL_FLAGS_SET;

    static std::uint64_t allWorkerFlags(unsigned int threadCount){
        constexpr unsigned int maximumThreads =
            std::numeric_limits<std::uint64_t>::digits;
        if(threadCount == 0 || threadCount > maximumThreads)
            throw std::invalid_argument("Parallel Tarjan supports 1 to 64 worker threads");
        if(threadCount == maximumThreads)
            return std::numeric_limits<std::uint64_t>::max();
        return (std::uint64_t{1} << threadCount) - 1;
    }
    
    //Data structures
    const Graph<Vid>& graph;
    Dictionary<Vid, WeakReference<Cell<Vid>>>&  dict;
    SuspensionManager susMgr;
    Pending& pending;
    StealingQueue& cellQueue;
    
    //Member Variables
    std::atomic<std::uint64_t> flags{0};
    
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
  
    
    MultiThreadedTarjan(const Graph<Vid>& _graph, Dictionary<Vid, WeakReference<Cell<Vid>>>& _dict, unsigned int num_threads , Pending& _pending, StealingQueue& _queue) : NUM_THREADS(num_threads), ALL_FLAGS_SET(allWorkerFlags(num_threads)), graph(_graph), dict(_dict), pending(_pending), cellQueue(_queue){
     ;
    }
   
    ~MultiThreadedTarjan();
    
    
};

#endif /* multiThreadedTarjan_hpp */

