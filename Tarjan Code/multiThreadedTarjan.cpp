//
//  multiThreadedTarjan.cpp
//  Tarjan
//
//  Created by Alex Zabrodskiy on 4/30/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#include "multiThreadedTarjan.hpp"
#include "tarjan.hpp"
#include <thread>
#include <stdio.h>
#include <chrono>
#include <iostream>


MultiThreadedTarjan::~MultiThreadedTarjan(){;}


SCC_Set* MultiThreadedTarjan::run()
{

    SCC_Set* SCCs = new SCC_Set;
    
    std::vector<Worker> workers;
    std::vector<std::thread> threads;
    
    for(int ID = 0; ID < NUM_THREADS; ++ID)
        workers.emplace_back(ID, *this, graph, dict);
    
    for(Worker& worker: workers)
        threads.emplace_back(std::ref(worker));
    
    
    for(std::thread& thread: threads) thread.join();
    
    for(Worker& worker: workers){
        SCCs->insert(SCCs->end(), worker.SCCs.begin(), worker.SCCs.end());
        // Weak cell/search pointers may still be inspected by another worker
        // during shutdown. Keep the underlying storage alive until all join.
        worker.cleanUp();
    }
    
//    using namespace std::chrono_literals;
//
//    std::this_thread::sleep_for(6s);
//    
     //std::cout << "trasnfers:  " <<  s <<std::endl;
//
//    
//    std::cout << "searches:  " <<  s2 <<std::endl;
//    
    s2 =  0; s = 0;
// 
    return SCCs;
}


/*Called by worker threads to retrieve a search to execute. We first check the Pending queue 
 if any searches are ready to be resumed. If Pending is empty,
 we select a cell that hasn't been explored yet from the CellQueue to start a new search. If
 there are no more cells to explore, the thread polls until either a search is resumed or
 the termination protocol (see activeSearches) shows that no search can run again.
 Returns nullptr when the worker should exit*/
Search* MultiThreadedTarjan::getSearch(Worker* worker){
    
    while(true){

        //First, see if there is a pending Search we can resume. It was counted
        //in activeSearches when it was added to Pending
        if(Search* search = pending.get())
            return search;
        
        //Otherwise, try to start new search by taking a cell off the Stealing queue
        if(!cellQueue.isEmpty()){
            
            //Count ourselves before taking a root, so that no worker can see zero
            //active searches while we hold the last root in hand
            activeSearches.fetch_add(1);
            
            WeakReference<Cell<Vid>> root(cellQueue.next(worker));
            
            /* We try to find a potential root cell on the stealing queue.
             * we need to try to conquer it before starting the search.
             * Otherwise, we could be starting a search on a cell already
             * in another search's stack! If the conquer suceeds, we can start the search
             * otherwise we continue this loop
             */
            
            if(root.get()){
                // The queue returns a weak, generation-tagged pointer. Another
                // worker may complete and recycle it before we try to claim it.
                Reference<Cell<Vid>> rootReference(root.get()->getReference(root.age));
                if(rootReference && rootReference->claimOrFail(worker->spareSearch)){ //Found a viable root for the new search
                    worker->initNeighbors(rootReference.getPtr());
                    
                    Search* search = worker->spareSearch;
                    search->setRoot(rootReference.getPtr());
                    worker->allocateSpareSearch();
                    return search;
                }
            }
            
            activeSearches.fetch_sub(1);
            continue; //Another search claimed the root first; try the next one
        }
        
        //Every root has been handed out and nothing is running or waiting to run.
        //Suspended searches can only be resumed by running ones, so we are done
        if(activeSearches.load() == 0)
            return nullptr;
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        
    }
    
}
