//
//  suspensionManager2.hpp
//  Tarjan
//
//  Created by Alex Zabrodskiy on 5/19/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#ifndef suspensionManager_hpp
#define suspensionManager_hpp

#include <stdio.h>
#include <mutex>
#include "stealingQueue.hpp"
#include <vector>
#include "worker.hpp"

#define SUSPEND true

#define RESUME false


class SuspensionManager{
    
    
private:
    
    /*Functions to suspend searches and retrieve which searches are suspended on which
     We abstact away the implementation of these functions from the rest of the class so that
     it is easy to change how we track suspended seaches by just altering these functions */
    
    inline void unSuspend(Search* const search){
        search->removeCellBlockedOn();
    }
    
    void runCellTransfer(const std::vector<Search*>& S, const std::vector<Cell<Vid>*>& C);
    
    
public:
    
    bool suspend(Worker& worker, Search* const Sn, Cell<Vid>* const conflictCell);
    
    void bulkUnsuspend(BlockedList<Search*>* const suspendedList, Cell<Vid>* const prevBlockedOn, std::vector<Search*>& toResume);
    
//    void bulkUnsuspendLF(BlockedList<Search*>* const suspendedList, Cell<Vid>* const prevBlockedOn, std::vector<Search*>& toResume){
//        
//        Search* search; const int size(suspendedList->size());
//        
//        //Cells will very rarely have more than a few searches suspended on them, so we optimize
//        //our algorithm to quickly deal with suspended. This allows us to save memory and time at the cost of
//        //slightly longer stores and retrievals when the suspended list is longer than 8.
//        //Because most graphs are large, it will be very rare when more than a few searches suspend on the same cell
//        //so the trade off is well worth it
//        
//        if(suspendedList->isSmallList(size)){
//            Search** const suspBuffer{suspendedList->getSmallBuffer()};
//            for(int e = 0; e < size; ++e){
//                search = suspBuffer[e];
//                if(search && search->removeCellBlockedOn(prevBlockedOn))
//                    toResume.push_back(search);
//            }
//        }
//        
//        else{
//            
//            auto suspendedListIt(suspendedList->getIt(size));
//            
//            while(suspendedListIt.increment()){
//                search = suspendedListIt.getNext();
//                
//                if(search && search->removeCellBlockedOn(prevBlockedOn))
//                    toResume.push_back(search);
//                
//            }
//            
//        }
//        
//        
//    }
    
    
};



#endif /* suspensionManager2_hpp */
