//
//  StealingQueue.hpp
//  Tarjan
//
//  Created by Alex Zabrodskiy on 5/7/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#ifndef StealingQueue_hpp
#define StealingQueue_hpp

#include <stdio.h>
#include <iostream>
#include "cell.h"
#include "worker.hpp"

class StealingQueue{

public:
    //Get a cell off the queue to explore
    virtual WeakReference<Cell<Vid>> next(Worker* const worker) = 0;
    
    virtual void bulkInsert(std::vector<Cell<Vid>*>& cells) = 0;

    virtual bool isEmpty() = 0;
    
    virtual void clear() = 0;

};

class UnrootedStealingQueue : public StealingQueue{
private:
    
    
    const Vid* const vertices;
    std::atomic<Vid> index{0};
    const Vid ttlCells;
    const long NUM_THREADS;
    Dictionary<Vid, WeakReference<Cell<Vid>>>& dict;
    
    
public:
    
    UnrootedStealingQueue(const Vid* const toExplore, Vid size, Dictionary<Vid, WeakReference<Cell<Vid>>>& _dict, long _NUM_THREADS);
    ~UnrootedStealingQueue();
    
    WeakReference<Cell<Vid>> next(Worker* const worker);
    
    inline bool isEmpty(){
        return index.load() >= ttlCells;
    }
    
    inline void clear(){
        index = 0; 
    }
    
    inline void bulkInsert(std::vector<Cell<Vid>*>& cells){
        
        throw std::exception();

    }
    
    
};







#endif /* StealingQueue_hpp */
