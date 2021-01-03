//
//  AdjacencyListGraph.h
//  Tarjan4
//
//  Created by Alex Zabrodskiy on 6/8/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#ifndef AdjacencyListGraph_h
#define AdjacencyListGraph_h


#include "graph.h"
#include <unordered_map>
#include "utilities.hpp"
#include <random>       // std::default_random_engine
#include "simpleClock.h"

template <class V>
class AdjacencyListGraph: public Graph<V>{
    
private:
    std::unordered_map<V, std::vector<V> > edges;
    V* vertexList;
    
    
public:
    AdjacencyListGraph() : vertexList(nullptr){;}
    AdjacencyListGraph(std::unordered_map<V, std::vector<V> >&& _edges) : edges(std::move(_edges)), vertexList(nullptr){
        delete &_edges; updateVertexArray(); }
    
    void insertVertex(V vertex);
    void insertEdge(V from, V to);
    const std::vector<V>& getNeighborsVector(V vertex)  const;
    std::unordered_set<V>* getVertices() const;
    inline int size() const {return (int) edges.size();}
    void removeVertex(V vertex);
    void removeEdge(V from, V to);
    void bulkInsertEdges(const std::initializer_list<std::pair<V,V>>& list);
    bool edgeExists(V from, V to) const ;
    V* getVerticesArray(Vid& size)   const;
    V* getVerticesArrayShuffled(Vid& size) const;
    void updateVertexArray();
    bool hasVertex(V vertex) const {return edges.count(vertex) > 0;} ;

    virtual ~AdjacencyListGraph(){
        delete[] vertexList;
    }
    
    virtual size_t numberEdges(){
        size_t toReturn{0};
        
        for (auto const& entry : edges)
            toReturn += (entry.second).size();
        
        
        return toReturn;
    }
    
};

/*Updates the array storing the graph's verticies. */
template <class V>
void AdjacencyListGraph<V>::updateVertexArray(){
    
    if(vertexList) //delete the old list of verticies
        delete vertexList;
    
    vertexList = new V[edges.size()];
    
    unsigned long v(0);
    for(auto& vertex: edges)
        vertexList[v++] = vertex.first;
    
    
}



template<class V>
V* AdjacencyListGraph<V>::getVerticesArray(Vid& size) const{
    
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
V* AdjacencyListGraph<V>::getVerticesArrayShuffled(Vid& size) const{
    
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
inline void AdjacencyListGraph<V>::insertVertex(V vertex){
    
    //Inserts the entry if it does not exist
    //The syntax is uncommon, but correct according to the documentation of unordered_map.
    //the below spawns can entry in the unordered map with vertex mapping to an empty vector
    edges[vertex];
   
    
    
}

template<class V>
inline void AdjacencyListGraph<V>::insertEdge(V from, V to){
    if(!edges.count(from)) //If the vertex is not in the graph, add it
        edges[from];
    
    (edges[from]).push_back(to);
    
}

template<class V>
const std::vector<V>& AdjacencyListGraph<V>::getNeighborsVector(V vertex) const {
    return edges.at(vertex);
}

template<class V>
std::unordered_set<V>* AdjacencyListGraph<V>::getVertices() const {
    
    std::unordered_set<V>* vertices = new std::unordered_set<V>;
    
    for(auto& vertex: edges)
        vertices->insert(vertex.first);
    
    return vertices;
    
}

template<class V>
void AdjacencyListGraph<V>::removeVertex(V vertex){
    edges.erase(vertex);
}

template<class V>
void AdjacencyListGraph<V>::removeEdge(V from, V to){
    
    std::vector<V>& vect(edges[from]);
    
    auto it = vect.begin();
    
    while(it != vect.end()){
        if(*it == to)
            it = vect.erase(it);

    }

}

template<class V>
void AdjacencyListGraph<V>::bulkInsertEdges(const std::initializer_list<std::pair<V,V>>& list){
    
    for(auto& pair: list)
        insertEdge(pair.first, pair.second);
    
    
}

template<class V>
bool AdjacencyListGraph<V>::edgeExists(V from, V to) const {
    if(!edges.count(from))
        return false;
    for(const V& element: edges.at(from))
        if(element == to)
            return true;
    
    return false;
    
}


#endif /* AdjacencyListGraph_h */
