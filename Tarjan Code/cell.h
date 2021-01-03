//
//  node.h
//  Tarjan
//
//  Created by Alex Zabrodskiy on 4/25/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#ifndef cell_h
#define cell_h

#include "typedefs.h"
#include <vector>
#include <mutex>
#include <iostream>
#include <algorithm>
#include "search.hpp"
#include "WeakReference.h"
#include "Reference.hpp"

template <class V>
struct Cell;

template <class V>
struct Cell{
    
private:
    
    BlockedList<Search*> blockedSearches; //Stores searches blocked on the cell
    
public:
    V           vertex;
    Vid         index;
    Vid         rank;
    Status      status;
    std::vector<WeakReference<Cell<V>>> neighborQueue; //Neighbors to visit
    ReferenceCounter<Cell<V>>  refCounter;
    std::atomic<Vid> age;

    Cell() : age(0), refCounter(this){ 
    }

    
    ~Cell(){}
    
    /************************************************************************************/
    
    inline bool isComplete(){
        return status == CellStatus::COMPLETE_CELL;
    }
    
    inline bool isComplete(const Vid expectedAge){
        return status == CellStatus::COMPLETE_CELL || age != expectedAge;
    }
    
    inline void addNeighbor(Cell<V>* const neighbor, const V succVertex){
        neighborQueue.emplace_back(neighbor, succVertex);
    }
    
    inline bool isNew(){
        return status == CellStatus::NEW_CELL;
    }
    
    inline bool isNew(const Vid expectedAge){
        return status == CellStatus::NEW_CELL && age == expectedAge;
    }
    
    static inline bool isUnclaimed(const WeakReference<Cell<V>>& ref){
        
        return  (ref.get()->status == CellStatus::NEW_CELL) || (ref.get()->status == CellStatus::COMPLETE_CELL) || (ref.get()->age != ref.age);

    }
    
    inline bool onStackOf(Search* const search){
        
        return search == status;
    }
    /* Pre: Must be the owner of the cell and all its neighbors must have been explored
     * This function effectivly retires the cell
     */
    inline void markComplete(){
        status = CellStatus::COMPLETE_CELL;
    }
    
    inline std::vector<Search*>* removeBlockedList(){
//        std::lock_guard<std::mutex> lock(blockedOnMutex); //Can prob remove sync if careful
//        std::vector<Search*>* toReturn(blockedOn);
//        blockedOn = nullptr;
//        return toReturn;
        return 0;
    }
    
    
    inline BlockedList<Search*>* getBlockedList(){
        return &blockedSearches;
        
    }
    
    
    inline void initIndex(const Vid idx){
        this->index = idx; this->rank = idx;
    }
    
    inline void promote(Vid rankUpdate){
        this->rank = std::min(this->rank, rankUpdate);
    }
    
    /*Synchronized methods
     ***********************************************************************************/
    
    /** Transfer: The function changes the stack label of the cell to another search, officially transfering
     * ownership of the cell. Delta gives the cell an updated index to match the indicies of the new stack.
     * We add any searches that were blocked on the cell to the passed in by reference vector
     *blockedSearches
     */
    
    
    //
    inline void transfer(const long delta, Search* const newOwner){
        rank +=delta; index += delta;
        status = newOwner;
    }
    
    //Thread safe version of the owner() method
    inline Search* getOwner(){
        
        Search* owner(status.load());
        
        if(owner == CellStatus::NEW_CELL || owner == CellStatus::COMPLETE_CELL)
            return nullptr;
        
        return owner;
    }
    
    
    inline void addToBlockedList(Search* const search){
        
        blockedSearches.push_back(search);
    
    }
    

    
    inline void blockSearch(Search* const search){
        
        
        blockedSearches.push_back(search);
    }

    
    //Returns true if and only if the search successfully took ownership of the cell
    //If conquer succeeds, the cell should be added to the conquerer's stack
    inline char claim(Search* const search){
        Search* newCell(CellStatus::NEW_CELL);
        
        if(status.load(std::memory_order_relaxed) == CellStatus::NEW_CELL &&
           status.compare_exchange_strong(newCell, search))
            return CellStatus::CLAIMED;
        
        if(status.load(std::memory_order_relaxed) == CellStatus::COMPLETE_CELL)
            return CellStatus::COMPLETE;
        
        return CellStatus::OCCUPIED;
    }
    
    
    /**Attemts to conquer the cell on behalf of the search conquerer so that it can be added
     *to conquerer's stack. It is similar to the function above, but it either succeeds in
     *conquering the cell and returns true or fails and returns false and does not
     *put the search on the cell;s blockedOn list.
     * The main difference between this function and the normal conquer is that if the search fails
     *to conquer the cell, nothing else happens so it is as if the search never tried.
     */
    bool claimOrFail(Search* const conquerer){
        Search* newCell(CellStatus::NEW_CELL);

        if(status == CellStatus::NEW_CELL &&
           status.compare_exchange_strong(newCell, conquerer))
            return true;
        
        
        return false;
    }
    
    //Currently unused function 
    bool claimOrFail(){
        Search* newCell(CellStatus::NEW_CELL);
        
        if(status == CellStatus::NEW_CELL &&
           status.compare_exchange_strong(newCell, CellStatus::COMPLETE_CELL))
            return true;
        
        
        return false;
    }
    
    /*** Methods Not Requiring Sychronization Due To Cell Ownship Precondition******/
    
    /* All methods in this section assume the cell is owned by the search calling the method*/
    
   
    //The purpose of the method is to label a cell which is reserved for a specific owner
    //and hence faces no race conditions from competing threads 
    inline void setOwner(Search* const newOwner){
        status.store(newOwner);
    }

    
    inline bool allNeighborsDone(){
        return neighborQueue.empty(); //the queue is set to null when it runs out of neighbors
    }
    
    /*Returns a neighbor of this node to be explored. 
     The function tries to choose a node that is not currently 
     being explored by another search if one exists.
     The function assumes the neighbors queue is nonempty*/
    inline WeakReference<Cell<V>> getBestNeighbor(){

        WeakReference<Cell<V>> candidate = neighborQueue.back();
        
        //If the next element in the queue is not owned by another search,
        //return it
        if(isUnclaimed(candidate) || candidate.get()->onStackOf(status))
            neighborQueue.pop_back();
        
        //otherwise try to find a cell in the queue that is not occupied by another
        //search to avoid conflict. If non found, we hace no choice but to return an occupied cell
        //Note that this is not sychronized, so the cell might become occupied by the time
        //we return. This is ok since we make no guarantee that we return an unoccupied cell,
        //we just try to do so as an optimization
        
        else{
            
            auto it = neighborQueue.begin();
            
            //return first unoccupied cell
            while(it != neighborQueue.end()){
                candidate = *it;
                
                //is this neighbor occupped? If the neighbor is unclaimed or owned
                //by the current search, we return it for inspection
                if(isUnclaimed(candidate) || candidate.get()->onStackOf(status))
                    break;
                ++it;
            }
            
            if(it == neighborQueue.end())
                neighborQueue.pop_back();
            
            else
                neighborQueue.erase(it);
        }
        
        
        return candidate;
    }
    
    /*Currently unused*/
    inline WeakReference<Cell<V>> getBestNeighbor2(){
        
        WeakReference<Cell<V>> candidate = neighborQueue.back();
        
        //If the next element in the queue is not owned by another search,
        //return it
        
        if(candidate.subject->isComplete(candidate.age)){
            neighborQueue.pop_back();
            if(neighborQueue.empty()) return WeakReference<Cell<V>>(nullptr,0);
        }
        
        
        else if(isUnclaimed(candidate) || candidate.get()->onStackOf(status)){
            neighborQueue.pop_back(); return candidate;
        }
        //otherwise try to find a cell in the queue that is not occupied by another
        //search to avoid conflict. If non found, we hace no choice but to return an occupied cell
        //Note that this is not sychronized, so the cell might become occupied by the time
        //we return. This is ok since we make no guarantee that we return an unoccupied cell,
        //we just try to do so as an optimization
        
        else{
            
            auto it = neighborQueue.begin();
            
            //return first unoccupied cell
            while(it != neighborQueue.end()){
                candidate = *it;
                
                if(candidate.subject->isComplete(candidate.age)){
                    it = neighborQueue.erase(it);
                    if(it == neighborQueue.end()) return WeakReference<Cell<V>>(nullptr,0);
                }
                
                //is this neighbor occupped? If the neighbor is unclaimed or owned
                //by the current search, we return it for inspection
                else if(isUnclaimed(candidate) || candidate.get()->onStackOf(status)){
                    neighborQueue.erase(it); break;
                }
                
                else ++it;
            }
            
//            if(it == neighborQueue.end())
//                neighborQueue.pop_back();
//            
//            else
//                neighborQueue.erase(it);
        }
        
        
        return candidate;
    }
    
    void recycle(){
        ++age;
        if(blockedSearches.size())
            (blockedSearches).reset();
        
    }
    
    static thread_local std::vector<Cell<V>*>* RECYCLED_CELLS;
    

    inline void initCell(){
        status = CellStatus::NEW_CELL;
        ++refCounter; //Creates an artifical reference that protects the cell from being recycled until its complete
    }

    //Allows the cell to be recycled when all its references run out
    inline void permitRecycling(){
        --refCounter;
    }
    
    
    void recycleThis(){
            RECYCLED_CELLS->push_back(this);
            recycle();
    }
    
    inline Reference<Cell<Vid>> getReference(Vid expectedAge){
        //Return an empty reference if the cell's age exceeds the expected age, as the cell is already complete
        return this->age == expectedAge? refCounter.createReference() : Reference<Cell<Vid>>::EmptyReference();
    }
    
    
};

template <class V>
thread_local std::vector<Cell<V>*>* Cell<V>::RECYCLED_CELLS = nullptr;


#endif /* node_h */
