//
//  dictionary.h
//  Tarjan
//
//  Created by Alex Zabrodskiy on 4/29/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#ifndef dictionary_h
#define dictionary_h


/** When a search encounters a vertex on the graph, it needs to check whether a Cell object has already
been created to represent that vertex. If a cell object already exists for that node, we use this dictionary
to retreive it, otherwise we store an Node representing the cell in this dictionary. Because multiple threads may
 use the dictionary at once and may even try to add a node representing the same vertex concurrently,
 any implementation of this interface must be thread safe
 */


template <class K, class V>
class Dictionary {
    
public:
    
    //Inserts a value into the hashtable if the key did not already exist in the dictionary.
    //The return is an pair object. The pair's second object is a boolean indictating whether the
    //insertion was succeeded (no element with same key) and the first object is the value for the key after
    // the put (it is the element put() tried to insert if insertion was successful,
    //otherwise it is the old value
    virtual std::pair<V,bool> put(const K& key,const V& value) = 0;
    
    
    virtual ~Dictionary(){;}
    
    //An implementation does not need to support this either

    virtual std::vector<K>* getKeys()      = 0;
    
    virtual std::vector<V>* getValues()    = 0;
    
    virtual bool contains(const K& key)    = 0;
    
    virtual void deleteValues()            = 0;
    
    //An implementation does not need to implement this function nor does it need to guarantee thread
    //safety
    virtual unsigned long size() {return -1;}
        
  
    
 };
 
 
 #endif /* dictionary_h */
