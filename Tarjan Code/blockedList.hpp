//
//  blockedList.hpp
//  Tarjan
//
//  Created by Alex Zabrodskiy on 5/28/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#ifndef blockedList_hpp
#define blockedList_hpp

#include <stdio.h>
#include <atomic>
#include <algorithm>
#include "typedefs.h"


template <class S, int P = 3, int N_BUFS = 8>
class BlockedListIt;


/**The BlockedList provides a lock-free data structure to store the searches blocked on each cell
  
  The data structure has an initial capacity of 2^P. The idea is to choose a P so that the blockedList
  rarely exceeds size 2^P and then optimize the list so that writing and reading these first 2^P elements is 
  is very fast.
 
  The initial buffer, firstBuffer, is a member variable of the class. This allows us to avoid pointer ndirection
  and extra mallocs by having the buffer's memory allocated as part of BlockedList
 
  Should the size of the list exceed capacity, blockedList allocates a new buffer of size
  sizeOfLastBuffer*(2^P). The elements in the old buffers are not moved, we simply use the old buffers
  in addition to the new ones. We hold a master List, 'buffers', which holds pointers to all the 
  auxilary buffers that needed to be allocated. The 'buffers' array is only allocated if the firstBuffer's
  capacity is exhausted with a compare_and_exchange(null, new AtomicPointerArray).
 
  New buffers are also allocated with a compare_and_exchange.
   
  The N_BUFS variable provides a limit on the total number of buffers can be allocated; going over the limit would
   result in an error.
 
  Items are inserted with the push_back(item) function. Items are inserted in order, the first one at index 0, second
  at index 1 and so on. 
 
  The atomic member variable 'head' gives the index of the last element inserted. To insert a new element,
  a thread calls getAndIncrement() on head which returns the index the thread should insert the new element.
 
  After receiving an index, we retrive the corresponding buffer the index is associeted with an then
  convert the index into a position on the buffer. We precompute some calculations to speed up these conversions
  and store them in the static arrays sums[] and powers[]. The initSums() function 
  does these calculations for each template once when the class is initialized in the beginning of
  the program
 
  When a cell becomes complete, it calls unsuspendAll() which iterates through its BlockedList to find
  out which searches may be blocked on the cell. When head < size of firstBuffer = BASE, all of 
  the elements are in the firstBuffer. In this case, unsuspendAll() simply iterates though the entries of the firstBuffer
  If head >= BASE, we know some of the auxilary buffers contain entries as well. In that case, we use a 
  blockedList iterator to read all the values in the data structure. It is possible to use the iterator 
  to retrive all the values in the list even when head < BASE, but it is a good optimization to avoid the neccesary overhead
  when possible.
 
  The blockedList does not support a delete operation. Instead when a cell finds a search in the blockedList,
  it verifies that the cell is actually complete before unsuspending the search. 
 
  The reset() operation should be called each time a cell is recycled to clear the contents of all the buffers
 

 */


//Template param S: The type stored in the list; we use Search*
//Template param P: Size of initial buffer = 2^P. Each additional buffer is sizeof(lastBufferAllocated)*2^p
//Template param N_BUFS: maximum number of buffers that can store the blockedList's elements

template <class S, int P = 3, int N_BUFS  = 8>
class BlockedList{
    
    friend class BlockedListIt<S,P,N_BUFS>;
    
    
private:

    typedef std::atomic<std::atomic<S>*>* AtomicPointerArray;
    
    static int sums[N_BUFS];
    static int powers[N_BUFS];
    
    const static int BASE; // = 2^P, the size of the firstBuffer
    
    std::atomic<int> head;
    
    std::atomic<S> firstBuffer[1<<P]; // BASE = 1 << P = 2^P
    
    //Atomic pointer to array of extra buffers, which are allocated if needed
    std::atomic<AtomicPointerArray> buffers;
    
    //Returns the buffer associated with the index 'num'
    inline int findBuffer(const int num){
        
        int loc{0};
        
        while(num >= sums[loc]) loc++;
        
        return loc;
    }
    
    //Precomputes calculations that determine which we buffer an index corresponds to
    static int  initSums(){
        int _base = 1 << P;
        
        sums[0]     = _base;
        powers[0]   = _base;
        
        for(int i = 1; i < N_BUFS; ++i){
            powers[i] = powers[i-1] << P;
            sums[i]   = sums[i-1] + powers[i];
        }
        
        return _base;
        
    }
    
public:

    
    BlockedList(): head(0), firstBuffer{}, buffers(nullptr){}

    //Deletes the buffers used to store elements in blockedList
    ~BlockedList(){
        if(buffers){
            for(int arr = 1; arr < N_BUFS;  ++arr){ //1st buf not dynamically allocated; dont delete
                if(buffers[arr])
                    delete[] buffers[arr].load();
            }
            
            delete[] buffers.load();
            
        }
    }
    /* Function: push_back()
     The push_back() function inserts @item to the back of the list.
     Head is incremented atomically and its previous value idx is retrieived. 
     The function will store item at the location represented by idx. 
     If idx < BASE, the item is inserted into firstBuffer[idx] and the function returns.
     
     Otherwise, findBuffer() is used to calculate the auxilary buffer and position
     idx corresponds to. Before inserting item into the buffer, the function verifies that the
     buffer actually exists. If the buffer does not exist, we allocate
     space for the buffer and use a compare_and_exchange() call to set the buffer to the one we created
     Afterward, the correct item is inserted into the correct location in the buffer.
     */
    
    inline void push_back(const S& item){
        int idx(head++); 
        
        //if index is less than base, we just insert the search in the correct location of firstBuffer
        if(idx < BASE){
     
            firstBuffer[idx].store(item, std::memory_order_relaxed);
            
            return;
            
        }
        //calculate the auxilary buffer and the index of the new buffer we must insert into
        int buffer (findBuffer(idx));
        
        int pos(idx - sums[buffer-1]);
        
        //Initialize the array of auxilary buffers if it has not yet been initilized
        //We also initialize the first auxilary buffer right away to avoid doing two compare_and_exchanges
        //since we would have had to do it anyway
        if(!buffers){
            
            AtomicPointerArray bufferArray = new std::atomic<std::atomic<S>*>[N_BUFS]();
            bufferArray[0] = firstBuffer;
            bufferArray[1] = new std::atomic<S>[powers[1]](); AtomicPointerArray null(nullptr);
            
            if(!buffers.compare_exchange_strong(null, bufferArray)){
                //If another thread added the auxilary buffers array first, we delete the one we created
                delete[] bufferArray[1].load();  delete[] bufferArray;
            }
        }
        
        //Allocate the buffer we plan to insert into if it has not yet been created
        if(!buffers[buffer]){
            //Create a new buffer b
            auto newBuffer(new std::atomic<S>[powers[buffer]]()); std::atomic<S>* null(nullptr);
            
            //If another thread has already added a buffer, we delete the one we created
            if(!buffers[buffer].compare_exchange_strong(null, newBuffer))
                delete[] newBuffer;
        }
        
        buffers[buffer][pos].store(item, std::memory_order_relaxed); //Insert the item
    }
    
    
    /*This function is used to reset the blockedlist. The function sets all of the list's entries by setting
     *them to null and resets head to zero*/
    inline void  reset(){
        /*Each time a cell is recycled, we fill all the existing buffers with null
        *to erase all entries from the cell's previous life */
        if(head){
            std::fill(firstBuffer, firstBuffer + BASE, nullptr);
            
            if(buffers){
                for(int buf = 1; buf < N_BUFS; ++buf)
                    if(buffers[buf])
                        std::fill(buffers[buf].load(), buffers[buf].load() + powers[buf], nullptr);
            }
            
            head = 0; //Reset head to zero
            
        }

    }
    
    inline int size(){
        return head.load();
    }
    //Is the list small enough that we can use the more efficient iterator to read its elements
    inline bool isSmallList(const int& _size){
        return _size <  BASE;
    }
    //Retrieves element efficiently
    inline S& getFromSmallList(const int idx){
        return firstBuffer[idx];
    }
    
    inline std::atomic<S>* getSmallBuffer(){
        return firstBuffer;
    }
    
    //get iterator to the blockedList
    BlockedListIt<S,P,N_BUFS> getIt(const int size){
        return BlockedListIt<S,P,N_BUFS>(*this, size);
    }
    
};

/* Iterator for the blockedList, used when the blockedList has more than 2^P entries
 
 
 */

template <class S, int P, int N_BUFS>
struct BlockedListIt{
    
private:
    int idx, arr, ttl;
    const int last;
    BlockedList<S,P,N_BUFS>& list;
    
public:
    
    BlockedListIt(BlockedList<S,P,N_BUFS>& _list, int _last): idx{-1}, arr{0}, last{_last}, list(_list), ttl{-1}{}
    
    inline S getNext(){
        
        if(!arr)
            return list.firstBuffer[idx];
        
        if(!list.buffers[arr])
            return nullptr;
        
        return list.buffers[arr][idx];
        
    }
    
    //Increments the iterator, returning true if the list has more elements to consume
    inline bool increment(){
        
        if(++ttl == last)
            return false;
        
        if(ttl == BlockedList<S,P, N_BUFS>::sums[arr]){
            ++arr; idx = 0;
            
            //If the array of spare buffers is not allocated, the iterator can produce no more values
            if(arr  ==  1 && !list.buffers){
                idx = last;  return  false;
            }
        }
        else
            ++idx;
        
        return true;
        
    }
    
};

template<class S, int P, int N_BUFS>
int BlockedList<S,P,N_BUFS>::sums[N_BUFS];

template<class S, int P, int N_BUFS>
int BlockedList<S,P,N_BUFS>::powers[N_BUFS];

template<class S, int P, int N_BUFS>
const int BlockedList<S,P,N_BUFS>::BASE = BlockedList<S,P,N_BUFS>::initSums();

#endif /* blockedList_hpp */
