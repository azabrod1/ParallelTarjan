//
//  directedHashGraph.h
//  Tarjan
//
//  Created by Alex Zabrodskiy on 4/24/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//
// Implements the graph class in an ajacency list representation
//

#ifndef directedHashGraph_h
#define directedHashGraph_h

#include "graph.h"
#include <unordered_map>
#include "utilities.hpp"
#include <random>       // std::default_random_engine
#include "simpleClock.h"
#include "AdjacencyListGraph.h"
template <class V>
class DirectedHashGraph: public Graph<V>{
    
private:
    std::unordered_map<V, std::unordered_set<V>* > edges;
    V* vertexList;
    
    
public:
    DirectedHashGraph(){;}
    DirectedHashGraph(std::unordered_map<V, std::unordered_set<V>* >&& _edges) : edges(std::move(_edges)){
        delete &_edges; updateVertexArray(); }
    
    void insertVertex(V vertex);
    void insertEdge(V from, V to);
    const std::unordered_set<V>& getNeighbors(V vertex) const;
    std::unordered_set<V>* getVertices() const;
    inline int size() const {return (int) edges.size();}
    void removeVertex(V vertex);
    void removeEdge(V from, V to);
    void bulkInsertEdges(const std::initializer_list<std::pair<V,V>>& list);
    bool edgeExists(V from, V to) const ;
    V* getVerticesArray(Vid& size)   const;
    V* getVerticesArrayShuffled(Vid& size) const;
    void updateVertexArray();
    bool hasVertex(V vertex) const {return edges.count(vertex) > 0;}
    virtual ~DirectedHashGraph(){
        //Delete the unordered sets of verticies
        for(auto& vertex: edges)
            delete vertex.second;
        
        delete[] vertexList;
        
    }
    
};

/*Updates the array storing a of the graph's verticies. */
template <class V>
void DirectedHashGraph<V>::updateVertexArray(){
    
    if(vertexList) //delete  the old list of verticies
        delete vertexList;
    
    vertexList = new V[edges.size()];
    
    unsigned long v(0);
    for(auto& vertex: edges)
        vertexList[v++] = vertex.first;

    
}





template<class V>
V* DirectedHashGraph<V>::getVerticesArray(Vid& size) const{
    
//    size = edges.size();
//    V* vertices = new V[size];
//    
//    unsigned long v(0);
//    for(auto& vertex: edges)
//        vertices[v++] = vertex.first;
    
    size = edges.size();
    

    return vertexList;
}

template<class V>
V* DirectedHashGraph<V>::getVerticesArrayShuffled(Vid& size) const{
    
    size = edges.size();
    V* vertices = new V[size];
    
    unsigned long v(0);
    for(auto& vertex: edges)
        vertices[v++] = vertex.first;
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    std::shuffle(vertices, vertices+size-1,  std::default_random_engine(seed)); 
    
    
    return vertices;
}

template<class V>
inline void DirectedHashGraph<V>::insertVertex(V vertex){
    
    //If the vertex is not in the graph, add it
    if(edges.count(vertex) == 0)
        edges.insert(std::make_pair(vertex, new std::unordered_set<V>));
    
    
}

template<class V>
inline void DirectedHashGraph<V>::insertEdge(V from, V to){
    
    if(edges.count(from) == 0) //If the vertex is not in the graph, add it
        edges[from] = new std::unordered_set<V>;
    
    if(edges.count(to) == 0) //If the vertex is not in the graph, add it
        edges[to] = new std::unordered_set<V>;
    
    edges[from]->insert(to);
    
}

template<class V>
const std::unordered_set<V>& DirectedHashGraph<V>::getNeighbors(V vertex) const {
    return *(edges.at(vertex));
}

template<class V>
std::unordered_set<V>* DirectedHashGraph<V>::getVertices() const {
    
    std::unordered_set<V>* vertices = new std::unordered_set<V>;
    
    for(auto& vertex: edges)
        vertices->insert(vertex.first);
        
    return vertices;
    
}

template<class V>
void DirectedHashGraph<V>::removeVertex(V vertex){
    delete edges[vertex];
    
    edges.erase(vertex);
}

template<class V>
void DirectedHashGraph<V>::removeEdge(V from, V to){
    
    edges[from]->erase(to);
}

template<class V>
void DirectedHashGraph<V>::bulkInsertEdges(const std::initializer_list<std::pair<V,V>>& list){
    
    for(auto& pair: list)
        insertEdge(pair.first, pair.second);
    
    
}

template<class V>
bool DirectedHashGraph<V>::edgeExists(V from, V to) const {
    return (edges.at(from)->count(to)) != 0;
}


#endif /* directedHashGraph_h */
