//
//  ShardedSpinLock.h
//  Tarjan4
//
//  Created by Alex Zabrodskiy on 6/7/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#ifndef ShardedSpinLock_h
#define ShardedSpinLock_h


#include "dictionary.h"
#include <mutex>
#include <unordered_map>
#include "worker.hpp"
/**Implements the sychronized dictionary class with sychronized blocks (a mutex)
 */


template <class K, class V>
class ShardedSpinLock : public Dictionary<K, V> {
    
private:
    const static int BITS = 10;
    const static int LOCKS = 2 << BITS;
    std::atomic_bool lockArray[LOCKS]{};
    std::unordered_map<K, V> map[LOCKS];
    
    inline void lock(const int key){
        
        while(true){
            while(lockArray[key].load(std::memory_order_relaxed)){};
            bool f(false);
            if(lockArray[key].compare_exchange_weak(f, true, std::memory_order_relaxed, std::memory_order_relaxed))
                return;
        }
        
    }
    
    inline bool tryLock(const int key){
        bool f(false);

        return !lockArray[key].load(std::memory_order_relaxed) && lockArray[key].compare_exchange_weak(f, true, std::memory_order_relaxed, std::memory_order_relaxed);
        
    
    }
    
    
    inline void unlock(const int key){
        
        lockArray[key].store(false, std::memory_order_relaxed);
        
    }
    
public:
    
    
    //Inserts a value into the hashtable if the key did no already exist in the dictionary.
    //Returns true if and only if the insertion is successful
    std::pair<V,bool> put(const K& key,const V& value){

        K key1 = key & (LOCKS - 1);
        K key2 = key >> BITS;
        
        
        lock(key1);
        
        auto val =  map[key1].insert(std::make_pair(key2, value));
        
        unlock(key1);
        
        
        return std::make_pair((val.first)->second, val.second);
        
    }
    
    void putAll(const std::vector<Vid>& vect, Cell<Vid>* cell, Worker& thread){
        
       // std::unordered_set<Vid> verts(vect.begin(), vect.end());
        
        std::vector<bool>& verts = thread.vAdded; verts.assign(vect.size(), false);
        int left(verts.size());
        
        while(left > 1){
            
            for(int v = 0; v < vect.size(); ++v){
                if(verts[v]) continue;
                
                int key = vect[v];
                Vid key1 = key & (LOCKS - 1);
                Vid key2 = key >> BITS;
                thread.spareCell->vertex = key;
                if(tryLock(key1)){
                    
                    auto status = map[key1].insert(std::make_pair(key2, WeakReference<Cell<Vid>>(thread.spareCell, thread.spareCell->age)));
                    unlock(key1);
                    
                    verts[v] = true; left--;
                    
                    if(status.second)
                        thread.allocateSpareCell();

                    
                    Cell<Vid>* neighbor = (status.first)->second.get();
                    int age = (status.first)->second.age;
                    
                    
                    if(!neighbor->isComplete(age)) //If the neighbor is complete, no search has to visit it again
                        cell->addNeighbor(neighbor, age);
                    
                    
                }
            
            }
        }
        
        for(int v = 0; v < vect.size(); ++v){
            if(verts[v]) continue;
        
            int key = vect[v];
        
            Vid key1 = key & (LOCKS - 1);
            Vid key2 = key >> BITS;
            thread.spareCell->vertex = key;
            
            lock(key1);
            
            auto status =  map[key1].insert(std::make_pair(key2,  WeakReference<Cell<Vid>>(thread.spareCell, thread.spareCell->age)));
            
            unlock(key1);
            
                if(status.second)
                    thread.allocateSpareCell();
                
                Cell<Vid>* neighbor = (status.first)->second.get();
                int age = (status.first)->second.age;
                
                
                if(!neighbor->isComplete(age)) //If the neighbor is complete, no search has to visit it again
                    cell->addNeighbor(neighbor, age);
                
            return;
        }
        

        
        /*
        while(verts.size() > 0){
            
            auto it = verts.begin();
            while(it != verts.end()){
                int key = *it;
                Vid key1 = key & (LOCKS - 1);
                Vid key2 = key >> BITS;
                thread.spareCell->vertex = key;
                if(tryLock(key1)){
                    
                    auto status = map[key1].insert(std::make_pair(key2, WeakReference<Cell<Vid>>(thread.spareCell, thread.spareCell->age)));
                    unlock(key1);
                    
                    if(status.second)
                        thread.allocateSpareCell();
                    
                    it = verts.erase(it);
                    
                    Cell<Vid>* neighbor = (status.first)->second.get();
                    int age = (status.first)->second.age;
                    
                    
                    if(!neighbor->isComplete(age)) //If the neighbor is complete, no search has to visit it again
                        cell->addNeighbor(neighbor, age);
                    
                    break;

                }
                
                else it++;
                    
            
            }
            
        }*/

        
    }
    
    
    V&    get(const K& key) {
        
        K key1 = key & (LOCKS - 1);
        K key2 = key >> BITS;
        
        lock(key1);
        
        auto& toReturn(map[key1].at(key2));
        
        unlock(key1);

        return toReturn;
        
    }
    
    //Same as get
    V&    operator[](const K& key){
        K key1 = key & (LOCKS - 1);
        K key2 = key >> BITS;
        
        lock(key1);
        
        auto& toReturn(map[key1].at(key2));
        
        unlock(key1);
        
        return toReturn;
    }
    
    unsigned long size(){
        
        unsigned long size(0);
        
        for(int m = 0; m < LOCKS; ++ m)
            size += map[m].size();
        
        return size;
        
    }
    
    bool contains(const K& key){
        K key1 = key & (LOCKS - 1);
        K key2 = key >> BITS;
        
        lock(key1);
        auto count(map[key1].count(key2));
        unlock(key1);
        return count;
    }
    
    std::vector<K>* getKeys(){
        
        std::vector<K>* toReturn = new std::vector<K>();
        toReturn->reserve(size());
        
        std::unordered_map<K, V>* curr;
        
        for(int m = 0; m < LOCKS; ++m){
            curr = map + m;
            
            for(auto& pair: *curr)
                toReturn->push_back(pair.first);
            
        }
        
        
        return toReturn;
    }
    
    std::vector<V>* getValues(){
        
        std::vector<V>* toReturn = new std::vector<V>();
        toReturn->reserve(size());
        
        std::unordered_map<K, V>* curr;
        
        for(int m = 0; m < LOCKS; ++m){
            curr = map + m;
            
            for(auto& pair: *curr)
                toReturn->push_back(pair.second);
            
            
        }
        return toReturn;
    }
    
    void deleteValues(){
        
        std::unordered_map<K, V>* curr;
        for(int m = 0; m < LOCKS; ++m){
            curr = map + m;
            
            for(auto& pair: *curr)
                delete pair.second.get();
            
        }
    }
    
    
    
};





#endif /* ShardedSpinLock_h */
