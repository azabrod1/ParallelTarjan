//
//  Worker.cpp
//  Tarjan
//
//  Created by Alex Zabrodskiy on 5/10/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#include "worker.hpp"
#include "multiThreadedTarjan.hpp"
#include  "Reference.hpp"
#include "ShardedSpinLock.h"


Worker::Worker(unsigned int _ID, MultiThreadedTarjan& _algo, const Graph<Vid>& _graph, Dictionary<Vid, WeakReference<Cell<Vid>>>& _dict) : ID(_ID), MASK(1LL<<_ID), scheduler(_algo), graph(_graph), dict(_dict), spareSearch(new Search) {
    
    allocateSpareCell();
    recycledCells.reserve(10);
}

void Worker::operator()(){
    
    Cell<Vid>::RECYCLED_CELLS = &recycledCells;
    
    Search* search;
    
    while(true){
        
        search = scheduler.getSearch(this);
        if(!search){
            cleanUp();
            return;
        }
        
        execute(search);
        
    }
}


void Worker::execute(Search* const search){
    Cell<Vid>* curr, *child;
    char attempt;
    
    while(!search->controlStackEmpty()){
        
        curr = search->controlStackTop();

        if(!curr->allNeighborsDone()){
            
            WeakReference<Cell<Vid>> succ(curr->getBestNeighbor());
            //Increment the reference count of the cell until
            //the reference goes out of scope so that the cell is not recyled while we
            //are using it
            
            Reference<Cell<Vid>> ref(succ.get()->getReference(succ.age));
            
            //The cell has  already been completed as it now has a different age than when  it had
            //the target vertex
            
            if(!ref || ref->isComplete()) continue;
            
            child = ref.getPtr();
        
            if(child->onStackOf(search)) //if on stack, update low link
                curr->promote(child->index);
                
            
            //Below conditional not synchronized, so need to handle the possible
            //case that the cell becomes complete after the conditional but before conquer
            
            else{
                
                attempt = child->claim(search);
                
                if(attempt == CellStatus::CLAIMED){
                    search->pushToStacks(child); //Put child on Tarjan and Control stack and assign cell an index
                    initNeighbors(child);
                }
                
                else if(attempt == CellStatus::OCCUPIED)
                    if(scheduler.suspend(this, search, child))
                        return;
                    
                
                
                //Do not need to do anything if cell is complete
            }
            
        }
        else{ //Cell has no more children left, time to backtrack
            
            search->controlStackPop();
            
            if(!search->controlStackEmpty())
                search->controlStackTop()->promote(curr->rank);
            
            if(curr->index == curr->rank){
                if(curr == search->tarjanStackTop())
                    buildSingletonSCC(search, curr);
                else
                    buildSCC(search, curr);
            }
            
        }
    }
    //We can only get here if the search's control stack becomes empty. Thus,
    //we can reclaim the memory from the finished search at this point
    
    reclaim(search);
    
}


/**
 Pre: The cell must be owned by the thread
 Post: The cell's queue of neighboring cells is created. Each vertex that is a neighbor
 of the vertex represented by parameter cell will have a unique corresponding
 cell object after this call exists
 
 @param cell Pointer to the cell whose neighbors we are identifying
 */

void Worker::initNeighbors(Cell<Vid>* cell){
    
   // initNeighbors2(cell);
    
    Vid age; Cell<Vid>* neighbor;
    //get set of all the vertex's neighbors
    const std::vector<Vid>& succs = graph.getNeighborsVector(cell->vertex);
    
    //create cell objects for the successors of vertex that have not yet been seen
    for(auto& succ: succs){
        
        spareCell->vertex = succ; //Set vertex
        auto status = dict.put(succ, WeakReference<Cell<Vid>>(spareCell,spareCell->age));
        
        //We used up the cell object to store the neighbor so we need a new one
        //for next time
        if(status.second)
            allocateSpareCell();
        


        neighbor = status.first.get();
        age = status.first.age;
        
        
        if(!neighbor->isComplete(age)) //If the neighbor is complete, no search has to visit it again
            cell->addNeighbor(neighbor, age);
        
    }
    
}

void Worker::initNeighbors2(Cell<Vid>* cell){
    
    ShardedSpinLock<Vid, WeakReference<Cell<Vid>>>* map = dynamic_cast<ShardedSpinLock<Vid, WeakReference<Cell<Vid>>>*>(&dict);

    map->putAll(graph.getNeighborsVector(cell->vertex), cell, *this);
    
}


void Worker::buildSCC(Search* const search, Cell<Vid>* const head){
    //unordered set to store the SCC in
    auto scc(new SCC);
    //vector of searches to resume after we mark the cells as complete

    TarjanStack* tarjanStack(search->getTS());
    
    tarjanStack->back()->markComplete();
    auto rit(tarjanStack->rbegin());
    
    while(*rit != head)
        (*(++rit))->markComplete();
    
    for(auto it = (rit+1).base(); it != tarjanStack->end(); ++it){
        scheduler.resumeAllBlockedOn(*it, S);
        (*it)->permitRecycling();
        scc->push_back((*it)->vertex);
    }
    
    tarjanStack->erase((rit+1).base(), tarjanStack->end());
    
    SCCs.push_back(scc);
}

void Worker::buildSingletonSCC(Search* const search, Cell<Vid>* const cell){
    search->tarjanStackPop();
    cell->markComplete();
    scheduler.resumeAllBlockedOn(cell, S);
    cell->permitRecycling();
    SCCs.push_back(new SCC{cell->vertex});
    
}

void Worker::buildSoloSCC(Cell<Vid>* const cell){
    cell->markComplete();
    scheduler.resumeAllBlockedOn(cell, S);
    cell->permitRecycling();
    SCCs.push_back(new SCC{cell->vertex});
} 

void Worker::cleanUp(){

    delete spareCell; delete spareSearch;
    
    for(Search* search: recycled)
        delete search;
    
    for(Cell<Vid>*  cell: recycledCells)
        delete cell;
        
}

void Worker::allocateSpareSearch(){
    if(recycled.empty())
        spareSearch = new Search;
    else{
        spareSearch = recycled.back();
        recycled.pop_back();
    }
}

void Worker::allocateSpareCell(){
    if(recycledCells.empty())
        spareCell = new Cell<Vid>;
    else{
        spareCell = recycledCells.back();
        recycledCells.pop_back();
    }
    
    spareCell->initCell();
    
}




