//
//  cuckooDict.h
//  Tarjan4
//
//  Created by Alex Zabrodskiy on 8/10/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#ifndef cuckooDict_h
#define cuckooDict_h

//
//  tbb_concurrent_map.h
//  Tarjan
//
//  Created by Alex Zabrodskiy on 5/17/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//



#include "dictionary.h"
#include "cuckoohash_map.hh"

template <class K, class V>
class CuckooMap : public Dictionary<K, V>{
    
private:
    cuckoohash_map<K, V> map;
    
public:
    
    std::pair<V,bool> put(const K& key,const V& value){
        
        auto val =  map.insert(key, value);
        auto toReturn = val? value : map.find(key);
        
        return std::make_pair(toReturn, val);
        
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
        return map.contains(key);
    }
    
    
    std::vector<K>* getKeys(){
        
        std::vector<K>* toReturn = new std::vector<K>();
        toReturn->reserve(map.size() + 1);
        
        
       // for(auto& pair: map)
       //     toReturn->push_back(pair.first);
        
        return toReturn;
    }
    
    std::vector<V>* getValues(){
        
        std::vector<V>* toReturn = new std::vector<V>();
        toReturn->reserve(map.size() + 1);
        
        
       // for(auto& pair: map)
        //    toReturn->push_back(pair.second);
        
        return toReturn;
    }
    
    void deleteValues(){
        //        for(auto& pair: map){
        //            delete pair.second;
        //        }
    }
    
    
};


#endif /* cuckooDict_h */











