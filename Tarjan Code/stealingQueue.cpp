//
//  StealingQueue.cpp
//  Tarjan
//
//  Created by Alex Zabrodskiy on 5/7/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#include "stealingQueue.hpp"

UnrootedStealingQueue::UnrootedStealingQueue(const Vid* const toExplore, Vid size, Dictionary<Vid, WeakReference<Cell<Vid>>>& _dict, long _NUM_THREADS) :
vertices(toExplore), ttlCells(size), dict(_dict), NUM_THREADS(_NUM_THREADS) {
    

}

UnrootedStealingQueue::~UnrootedStealingQueue(){
    
}

WeakReference<Cell<Vid>>  UnrootedStealingQueue::next(Worker* const worker){
    Vid next(index++);
    
    while(next < ttlCells){
        Cell<Vid>* const toPut(worker->spareCell);
        toPut->vertex = vertices[next];
        auto status = dict.put(vertices[next], WeakReference<Cell<Vid>>(toPut, toPut->age));
        
        if(status.second) //If we used up the cell, allocate a replacement
            worker->allocateSpareCell();
        
        if(status.first.get()->isNew(status.first.age))
                return status.first;

        next = index++;
    }
    return nullWeakReference; //All done with cells
}
