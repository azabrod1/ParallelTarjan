//
//  blockedList.h
//  Tarjan4
//
//  Created by Alex Zabrodskiy on 7/31/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#ifndef OpenShardedMap_hpp
#define OpenShardedMap_hpp

#include "openAddressed.h"
//#include "tbb_concurrent_map.h"
#include "cuckooDict.h"

class OpenAddressedShardedMap   : public Dictionary<Vid, WeakReference<Cell<Vid>>>{
private:
    
    const static int BITS = 12;
    const static int SHARDS = 2 << BITS;
    OpenAddressedMap shard[SHARDS];
    
   
public:
    
  
    
    virtual std::pair<WeakReference<Cell<Vid>>,bool> put(const Vid& key, const WeakReference<Cell<Vid>>& value){
        
        //Inserts a value into the hashtable if the key did no already exist in the dictionary.
        //Returns true if and only if the insertion is successful
            Vid _key = getHash(key);
            Vid key1 = _key & (SHARDS - 1);
            Vid key2 = _key >> BITS;
        
        
            return shard[key1].put(key2,value);

    }
    
    inline Vid getHash(Vid x) {
        x = ((x >> 16) ^ x) * 0x45d9f3b;
        x = ((x >> 16) ^ x) * 0x45d9f3b;
        x = (x >> 16) ^ x;
        return x;
    }


    
    virtual ~OpenAddressedShardedMap(){}
    
    virtual std::vector<Vid>* getKeys(){return nullptr;};
    
    virtual std::vector<WeakReference<Cell<Vid>>>* getValues(){return nullptr;};
    
    //An implementation does not need to support this either
    virtual bool contains(const Vid& key){return 0;}
    
    virtual void deleteValues(){;}
    
    //An implementation does not need to implement this function nor does it need to guarantee thread
    //safety
    virtual unsigned long size() {return -1;}
    
    
};


#endif
