//
//  ReferenceCounter.h
//  Tarjan4
//
//  Created by Alex Zabrodskiy on 6/3/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#ifndef ReferenceCounter_h
#define ReferenceCounter_h

#include "typedefs.h"

template <class V> class Cell;

template <class E> class ReferenceCounter;

#define EMPTY_REFERENCE Reference<E>(nullptr)


template <class E>
struct Reference{
    
    friend class ReferenceCounter<E>;

    ReferenceCounter<E>*  refCounter;
    
    inline Reference() :  refCounter(nullptr){;}

    ~Reference(){
        if(refCounter)
            refCounter->decrement();
    }
    
    inline operator bool() const{
        return bool(refCounter);
    }
    
    inline E* operator->(){
        return refCounter->object;
    }
    
    inline E* getPtr(){
        return refCounter->object;
    }
    
    static inline Reference<E> EmptyReference(){
        return EMPTY_REFERENCE;
    }
    
    Reference<E> ( Reference<E> && that): refCounter(that.refCounter) {
        that.refCounter = nullptr;
    }
    
    Reference<E> ( Reference<E> & that) = delete;
  
    
private:
    
    inline Reference(ReferenceCounter<E>* const _counter) : refCounter(_counter){}
    
};




template <class E>
class ReferenceCounter {
    
    friend class Reference<E>;
    
private:
    std::atomic<int> references;
    E* const object;
    
public:
   
    ReferenceCounter(E* const _object) : object(_object), references(0){;}
    
    /*Creates a reference object that decrements the reference count when it goes out of scope*/
    Reference<E> createReference(){
        int refCount{references};
        
        //If there are zero references to the object, return false becuase the object
        //is currently treated as 'deleted'
        if(!refCount)
            return EMPTY_REFERENCE;
        
        if(references.compare_exchange_weak(refCount, refCount + 1, std::memory_order_relaxed, std::memory_order_relaxed))
            return Reference<E>(this); //Uses Return value optimization to avoid extra increment/decrement
        
        
        //Keep trying if the Compare-Exchange operation fails the first time
        do{
            
            refCount =  references.load();
            
            if(!refCount)
                return EMPTY_REFERENCE;
            
            if(references.compare_exchange_weak(refCount, refCount + 1, std::memory_order_relaxed, std::memory_order_relaxed))
                return Reference<E>(this);
            

        }while(true);
    
    }
    
    inline void decrement(){
        if(--references == 0)
            deleter();

    }
    
    //Same as decrement
    inline void operator--(){
        
        if(--references == 0)
            deleter();
        
        
    }
    
     void deleter();
    
    //Unconditionally increments the reference count without returning a reference. This should be used with
    //care of course. For instance, we can use this to create 'artificial references' to delay to recycing of cell objects
    //until the cell becomes complete without creating an actual refernece object
    inline void operator++(){
        references++;
    }
    
    int getNumReferences(){
        return references.load();
    }
    
    
};

template<> void ReferenceCounter<Cell<Vid>>::deleter();

template<class  E> void ReferenceCounter<E>::deleter(){
    delete object;
}


#endif /* ReferenceCounter_h */
