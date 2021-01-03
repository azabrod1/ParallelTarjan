//
//  tbb_concurrent_map.h
//  Tarjan
//
//  Created by Alex Zabrodskiy on 5/17/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//


#ifndef tbb_concurrent_map_h
#define tbb_concurrent_map_h
#include "dictionary.h"
#include "tbb/concurrent_unordered_map.h"


template <class K, class V>
class TBB_Concurrent_Map : public Dictionary<K, V>{
    
private:
    tbb::concurrent_unordered_map<K, V> map;
    
public:
    
    std::pair<V,bool> put(const K& key,const V& value){
        
        auto val =  map.insert(std::make_pair(key, value));
        
        return std::make_pair((*(val.first)).second, val.second);
        
    }
    
    V&    get(const K& key) {
        return map.at(key);
    }
    
    //Same as get
    V&    operator[](const K& key){
        return map.at(key);
    }
    
    unsigned long size(){
        return map.size();
    }
    
    bool contains(const K& key){
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
//        for(auto& pair: map){
//            delete pair.second;
//        }
    }


};











#endif /* tbb_concurrent_map_h */



