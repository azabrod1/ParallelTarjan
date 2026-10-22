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
#include <stdexcept>


class MultiThreadedTarjan{
    

private:
    
    //Constants
    const unsigned int NUM_THREADS;

    static unsigned int validThreadCount(unsigned int threadCount){
        if(threadCount == 0)
            throw std::invalid_argument("Parallel Tarjan needs at least one worker thread");
        return threadCount;
    }
    
    //Data structures
    const Graph<Vid>& graph;
    Dictionary<Vid, WeakReference<Cell<Vid>>>&  dict;
    SuspensionManager susMgr;
    Pending& pending;
    StealingQueue& cellQueue;
    
    //Member Variables
    
    /* Termination protocol. activeSearches counts the searches that are running
     * or waiting in Pending to be resumed, plus workers that are in the middle of
     * claiming a new root. Suspended searches are not counted: only a running
     * search can complete the cell a suspended search waits on. A search is
     * counted before it is added to Pending and uncounted only after its worker
     * stops executing it, so the count cannot reach zero while any search can
     * still become runnable. Once the root queue is exhausted and the count is
     * zero, no search can ever run again and every worker may exit.
     */
    std::atomic<std::int64_t> activeSearches{0};
    
public:
    
    SCC_Set*  run();

    Search* getSearch(Worker* worker);
    
    //Called by a worker after execute() returns, whether its search finished or suspended
    inline void searchStopped(){
        activeSearches.fetch_sub(1);
    }
    

    inline bool suspend(Worker* worker, Search* const S, Cell<Vid>* conflictCell){
        return susMgr.suspend(*worker, S, conflictCell);
    }
    
    
    inline void resumeAllBlockedOn(Cell<Vid>* const completeCell, std::vector<Search*>& toResume){
        BlockedList<Search*>* suspendedList(completeCell->getBlockedList());
        
        int size(suspendedList->size());
        
        if(size){
            toResume.clear();
            susMgr.bulkUnsuspend(suspendedList, completeCell, toResume);
            
            if(!toResume.empty()){
                //Count the searches before they become visible to other workers
                activeSearches.fetch_add(static_cast<std::int64_t>(toResume.size()));
                pending.addPending(&toResume);
            }
            
        }
        
    }
  
    
    MultiThreadedTarjan(const Graph<Vid>& _graph, Dictionary<Vid, WeakReference<Cell<Vid>>>& _dict, unsigned int num_threads , Pending& _pending, StealingQueue& _queue) : NUM_THREADS(validThreadCount(num_threads)), graph(_graph), dict(_dict), pending(_pending), cellQueue(_queue){
     ;
    }
   
    ~MultiThreadedTarjan();
    
    
};

#endif /* multiThreadedTarjan_hpp */

