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
    
    for(Worker& worker: workers)
        SCCs->insert(SCCs->end(), worker.SCCs.begin(), worker.SCCs.end());
    
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
 there are no more cells to explore, the thread pauses and contacts the termination protocal*/
Search* MultiThreadedTarjan::getSearch(Worker* worker){
    Search* search;
    flags.store(0); //Can prob use relaxed mem order here
    bool updateFlagsASAP(true); //Should we update flags next loop iteration
    bool done(false);
    const long mask(worker->MASK);
    
    while(!done){

        //First, see if there is a pending Search we can resume
        search = pending.get();
        
        if(search) return search;
        
        //Otherwise, try to start new search by taking a cell off the Stealing queue
        WeakReference<Cell<Vid>> root(cellQueue.next(worker));
        
        /* We try to find a potential root cell on the stealing queue.
         * we need to try to conquer it before starting the search.
         * Otherwise, we could be starting a search on a cell already
         * in another search's stack! If the conquer suceeds, we can start the search
         * otherwise we continue this loop
         */
        
        
        if(root.get())
            if(root.get()->claimOrFail(worker->spareSearch)){ //Found a viable root for the new search
                worker->initNeighbors(root.get());

                
                search = worker->spareSearch;
                search->setRoot(root.get());
                worker->allocateSpareSearch();
                return search;
            }
            
         
        
        if(updateFlagsASAP)
            flags|=mask;
        
        //Update flags next iteration if they were not updated this time and our flag is not set
        updateFlagsASAP = (!updateFlagsASAP && ((flags.load() & mask) == 0));
        
        done = (flags.load() == ALL_FLAGS_SET);
        
        if(!done){
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

        }
        
    }
    
    return nullptr;
    
}
