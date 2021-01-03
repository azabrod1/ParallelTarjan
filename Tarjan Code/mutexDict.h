//
//  MutexDict.h
//  Tarjan
//
//  Created by Alex Zabrodskiy on 4/29/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#ifndef mutexDict_h
#define mutexDict_h

#include "dictionary.h"
#include <mutex>
#include <unordered_map>
/**Implements the sychronized dictionary class with sychronized blocks (a mutex) 
 This implementation is likely to be much slower than a lock free one */


template <class K, class V>
class MutexDict : public Dictionary<K, V> {
    
private:
    std::mutex mu;
    std::unordered_map<K, V> map;
    
public:
    
    //Inserts a value into the hashtable if the key did no already exist in the dictionary.
    //Returns true if and only if the insertion is successful
    std::pair<V,bool> put(const K& key,const V& value){
        std::lock_guard<std::mutex> lock(mu);
        
        auto val =  map.insert(std::make_pair(key, value));
        
        return std::make_pair((*(val.first)).second, val.second);
        
    }
    
    
    V&    get(const K& key) {
        std::lock_guard<std::mutex> lock(mu);
        return map.at(key);
        
    }
    
    //Same as get
    V&    operator[](const K& key){
        std::lock_guard<std::mutex> lock(mu);
        return map.at(key);
    }
    
    unsigned long size(){
        std::lock_guard<std::mutex> lock(mu);
        return map.size();
    }
    
    bool contains(const K& key){
        std::lock_guard<std::mutex> lock(mu);
        return map.count(key) != 0;
    }
    
    std::vector<K>* getKeys(){
        
        std::vector<K>* toReturn = new std::vector<K>();
        toReturn->reserve(map.size() + 1);

        
        for(auto& pair: map)
            toReturn->push_back(pair.first);
        
        return toReturn;
    }
    
    std::vector<V>* getValues(){
        
        std::vector<V>* toReturn = new std::vector<V>();
        toReturn->reserve(map.size() + 1);
        
        
        for(auto& pair: map)
            toReturn->push_back(pair.second);
        
        return toReturn;
    }
    
    void deleteValues(){
     
        
    }
    
};


#endif /* MutexDict_h */
