//
//  pending.hpp
//  Tarjan
//
//  Created by Alex Zabrodskiy on 5/7/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#ifndef pending_hpp
#define pending_hpp

#include <stdio.h>
#include "stealingQueue.hpp"
#include "typedefs.h"
#include <deque>
//#include "tbb/concurrent_queue.h"
#include "ConcurrentQueue.h"

class Pending{
    
public:
    

    virtual void addPending(std::vector<Search*>* const toAdd) = 0;
    
    virtual void addPending(Search* const search) = 0;
    
    virtual Search* get() = 0;
    
    virtual bool isDone() = 0;
    
    virtual ~Pending(){;}
    
};


class LockedPendingQueue : public Pending{
    
    std::mutex  mu;
    std::deque<Search*> waiting;
    
public:
    
    inline void addPending(Search* search){
        std::lock_guard<std::mutex> lock(mu);
        waiting.push_back(search);
    }
    
    inline void addPending(std::vector<Search*>* const toAdd){
        std::lock_guard<std::mutex> lock(mu);
         for(Search* search: *toAdd)
             if(search) //The ones that do not need to be resumed are marked null by the suspension manager
                 waiting.push_back(search);

    }
    
    Search* get(){
        if(!waiting.empty()){
            std::lock_guard<std::mutex> lock(mu);
            if(!waiting.empty()){
                Search* toReturn = waiting.front();
                waiting.pop_front();
                return toReturn;
            }
        }
        
        return nullptr;
    }
    
    
    bool isDone(){
        return waiting.empty();
    }
    
};
/* //unncomment if TBB package installed
 
class LockFreePendingQueue : public Pending{
    tbb::concurrent_queue<Search*> waiting;
    
    inline void addPending(Search* search){
        waiting.push(search);
    }
    
    inline void addPending(std::vector<Search*>* const toAdd){
        for(Search* search: *toAdd)
            if(search)
                waiting.push(search);
        
    }
    
    Search* get(){
        
        Search* toReturn;
        if(waiting.try_pop(toReturn))
            return toReturn;
        else
            return nullptr;
    }
    
    bool isDone(){
        return waiting.empty();
    }
    
};*/

class LockFreePendingQueue : public Pending{
    moodycamel::ConcurrentQueue<Search*> waiting;
    inline void addPending(Search* search){
        waiting.enqueue(search);
    }
    
    inline void addPending(std::vector<Search*>* const toAdd){
        for(Search* search: *toAdd)
            if(search)
                waiting.enqueue(search);
        
    }
    
    Search* get(){
        
        Search* toReturn;
        if(waiting.try_dequeue(toReturn))
            return toReturn;
        else
            return nullptr;
    }
    
    bool isDone(){
        return waiting.size_approx() == 0;
    }
    
};





#endif /* pending_hpp */
