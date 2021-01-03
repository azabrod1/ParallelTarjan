//
//  graph.h
//  Tarjan
//
//  Created by Alex Zabrodskiy on 4/24/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#ifndef graph_h
#define graph_h

#include <unordered_set>
#include <iostream>
#include "typedefs.h"

template <class V>
class Graph{
    

public:
    
    //Pure virtuals to be implemented by graph implementations 
    virtual void insertVertex(V vertex)                                 = 0;
    virtual void insertEdge(V from, V to)                               = 0;
   
    virtual       std::unordered_set<V>* getVertices()          const   = 0;
    virtual       V* getVerticesArray(Vid& size)                const   = 0;
    virtual int size()                                          const   = 0;
    virtual void removeVertex(V vertex)                                 = 0;
    virtual void removeEdge(V from, V to)                               = 0;
    virtual bool edgeExists(V from, V to)                       const   = 0;
    virtual void updateVertexArray() {;}
    virtual bool hasVertex(V vertex) const {return true;}
    virtual ~Graph(){;};
    
    virtual const std::unordered_set<V>& getNeighbors(V vertex) const
    {throw std::exception(); }
    virtual const std::vector<V>& getNeighborsVector(V vertex)  const
    {throw std::exception(); }
    
    virtual size_t numberEdges(){return -1;}
    
    //Methods with a standard implementation
    virtual void bulkInsertEdges(const std::initializer_list<std::pair<V,V>>& list) {std::cout << "Not implemented" << std::endl;}
    virtual void printGraph(){
        for(auto& v1: *getVertices()){
            const std::unordered_set<V>& neighbors = getNeighbors(v1);
            if(neighbors.size() == 0) continue;
            
            for(auto& v2: neighbors)
                std::cout << v1 << "--->" << v2 << ", ";
            
            std::cout << std::endl;
        }
    }
    
    
    
};

#endif 
/* graph_h */



