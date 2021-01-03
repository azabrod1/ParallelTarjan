//
//  openAddressedSharded.h
//  Tarjan4
//
//  Created by Alex Zabrodskiy on 7/30/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#ifndef openAddressedSharded_h
#define openAddressedSharded_h

#include "dictionary.h"

struct Entry{
    Entry(const Vid _key, const Vid _age, Cell<Vid>* const _cell ) noexcept : key(_key), age(_age), cell(_cell){;}
    Entry( ) noexcept : key(0), age(0), cell(0){;}

    const Vid key, age;
    Cell<Vid>* const cell;
};


class OpenAddressedMap : public Dictionary<Vid, WeakReference<Cell<Vid>>>{
private:
    
    std::atomic<Entry>* table;
    std::atomic<Vid> capacity;
    std::atomic<Vid> size;
    std::atomic<int> members;
    

public:
    
    OpenAddressedMap(int init_pow_2 = 11) : size(1), members(0), capacity(1 << (init_pow_2)){
        table = new std::atomic<Entry>[1 << (init_pow_2)]();
    }
    
    virtual std::pair<WeakReference<Cell<Vid>>,bool> put(const Vid& key, const WeakReference<Cell<Vid>>& value){
        
        Vid currSize;
        do{
            
            currSize = size.load(); if(!currSize) continue; //resize in progress
            
            if(currSize*2 >= capacity){
                if(!members && size.compare_exchange_weak(currSize, 0)){
                    if(members || size){
                        size += currSize; continue;
                    }
                    resize(); size = currSize;
                }
                else
                    continue;
            }

            members++;
            currSize = size.load();
            
            if(currSize*2 >= capacity || !currSize) --members;
            else break;
                
            
        }while(true);
        
        Vid cap(capacity), location(getHash(key, cap));
        
        while(true){
            
            Entry curr = table[location].load();
            if(curr.cell){

                if(curr.key == key){ //Entry exists
                    
                    --members;
                    return std::make_pair<WeakReference<Cell<Vid>>,bool>(WeakReference<Cell<Vid>>(curr.cell, curr.age), false);

                    }
                location = (location + 1) % cap; continue;
            }
            
        
            Entry toPut(key, value.age, value.get()); Entry empty;
             if(table[location].compare_exchange_weak(empty, toPut)){ //successs
                 ++size; --members;
                 return std::make_pair<WeakReference<Cell<Vid>>,bool>(WeakReference<Cell<Vid>>(value.get(), value.age), true);
             }
        }
    
    }
 

    inline Vid getHash(const Vid key, const Vid cap){
        return key % cap;
    }
    
    void resize(){
        const Vid newCap(capacity*2); const Vid oldCap = capacity;
        std::atomic<Entry>* newTable = new std::atomic<Entry>[newCap]();
       
        Entry ent;
     
        Vid location;
        for(int e = 0; e < oldCap; ++e){
            Entry entry = table[e].load();
            if(entry.cell){
                location = getHash(entry.key, newCap);
                while(true){
                    Entry curr = newTable[location].load();
                    if(curr.cell)
                        location = (1 + location) % newCap;
                    else{
                        newTable[location].store(entry);
                        break;
                    }

                }
            }
            
        }
        delete[] table;
        
        table = newTable;
        
        capacity.store(newCap);
        
    }
    
    
    
    
    virtual ~OpenAddressedMap(){delete[] table;}
    
    virtual std::vector<Vid>* getKeys(){return nullptr;};
    
    virtual std::vector<WeakReference<Cell<Vid>>>* getValues(){return nullptr;};
    
    //An implementation does not need to support this either
    virtual bool contains(const Vid& key){return 0;}
    
    virtual void deleteValues(){;}
    
    //An implementation does not need to implement this function nor does it need to guarantee thread
    //safety
    virtual unsigned long getSize() {return -1;}
    
    
};



#endif /* openAddressedSharded_h */
