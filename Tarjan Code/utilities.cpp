//
//  utilities.cpp
//  Tarjan
//
//  Created by Alex Zabrodskiy on 4/28/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//



#include "utilities.hpp"
#include <math.h>
#include <stdlib.h>
#include <fstream>
#include <sstream>

std::random_device rd;

std::mt19937 Utility::engine(rd()); //Select random number generator engine

std::uniform_real_distribution<double> Utility::dist(0,1);

bool Utility::initialized = false;

/**Generates random graph with cardinality verticies. For any two vertices v,w we create an edge from v to w with probabilty edgeProb*/

Graph<Vid>* Utility::generateRandomGraph(double edgeProb, const Vid& cardinality){
    
    Graph<Vid>* graph = new DirectedHashGraph<Vid>;
    
    for(Vid v1 = 0; v1 < cardinality; ++v1)
        for(Vid v2 = 0; v2 < cardinality; ++v2)
            if(dist(engine) < edgeProb)
                graph->insertEdge(v1, v2);
    
    graph->updateVertexArray();
    return graph;
    
    
}


/** Many graphs exhibit "clustoring" of verticies. In other words, there are verticies that have many edges amongest each other
 * For example, consider a graph representing a social network. If Alice and Bob are friends and Bob and Olivia are friends, there is a relatively high probability that Alice and Olivia are friends. Most social networks use this realization to suggest connections to people one might know. This function generates a graph that exhibits this clustering property.
 *  We do this by assigning each vertex a "location" on the unit square. The closer (roughly euclidean distance) a vertex is to another,
 *  the higher the probability it will have an edge into that vertex.
 *  Let verticies v1 and v2 be two verticies with X coodinates dx apart and Y coordinates dy apart.
 *  We have that E = sqrt(dx^2+dy^2) is the Euclidean distance between the two points
 *  The function takes parameters 0<=Corr<=1 and edgeProb. A higher value for corr results in graphs with vertices that are more “clustered together”
 *   while Corr=0 results in the same function as the one above. The constant edgeProb is used to determine how dense the graph is.
 *
 *  The probability that v1 will have an edge into v2 is:
 
 *   P(v1,v2) = edgeProb([(1- sqrt(E))* corr + (1-corr)]    =  edgeProb(1 - C*sqrt(E))
 *
 * It is easy to see how corr acts as a "weight" and edgeProb determines density. The extra square root is not a mistake, we want to penalize far away edges more than the
 * standard eucldean distance. Note that we treat negative probabilities as 0.
 
 @return A pointer to a graph with the properties above
 */

Graph<Vid>* Utility::GeoGenerateRandomGraph(double edgeProb, const double& corr, const Vid& cardinality){
    
    Graph<Vid>* graph = new AdjacencyListGraph<Vid>;
    
    double* xCoordinates = new double[cardinality];
    double* yCoordinates = new double[cardinality];
    
    //Assign each coordinate a random location on the unit square ( 0<x<1, 0<y<1)
    for(int v = 0; v < cardinality; ++v){
        xCoordinates[v] = dist(engine); yCoordinates[v] = dist(engine);
    }
    
    double distance, dx, dy;
    
    for(Vid v1 = 0; v1 < cardinality; ++v1){
        graph->insertVertex(v1);
        for(Vid v2 = 0; v2 < cardinality; ++v2){
            
            if(v1 == v2) continue;
            
            dx = std::abs(xCoordinates[v1] - xCoordinates[v2]);  dy = std::abs(yCoordinates[v1] - yCoordinates[v2]);
            
            distance = sqrt(sqrt(sqrt(sqrt(dx*dx + dy*dy)))); //Almost euclidean metric but we take to 1/4 power instead 1/4 to further "punish" far away values
            
            if(dist(engine) < edgeProb*(1-corr*distance))
                graph->insertEdge(v1, v2);
            
        }
    }
    
    delete[] xCoordinates; delete[] yCoordinates;
    graph->updateVertexArray();
    return graph;
}

std::unordered_set<Vid>* Utility::missMatches(SCC_Set* s1, SCC_Set* s2){
    /*
    std::unordered_set<Vid>* misses(new std::unordered_set<Vid>);
    
    for(std::unordered_set<Vid>* x: *s1){
        
        bool matched(false);
        
        for(std::unordered_set<Vid>* y: *s2){
            
            bool common(false), uncommon(false);
            
            for(Vid e: *x)
                if(y->count(e))
                    common = true;
                else
                    uncommon = true;
            
            if(common && (uncommon || x->size() != y->size() )){
                for(Vid n: *x)
                    misses->insert(n);
                
                for(Vid n: *y)
                    misses->insert(n);
            }
            else if(common) matched = true;
        }
        if(!matched)
            for(Vid n: *x)
                misses->insert(n);
        
        
    }
    
    */
    return nullptr;
}



void Utility::deleteSCCs(SCC_Set* SCCs){
    
    for(auto set: *SCCs)
        delete set;
    delete SCCs;
}

//Difference of biggest sets
std::unordered_set<Vid>* Utility::bigDif(SCC_Set* s1, SCC_Set* s2){
//    
//    std::unordered_set<Vid>* dif(new std::unordered_set<Vid>);
//    
//    unsigned long max(0); std::unordered_set<Vid>* biggest1(0);
//    
//    if(s1->empty() || s2->empty()){
//        std::cout << "at least one set empty "  << std::endl;
//        return dif;
//    }
//    
//    for(auto set: *s1){
//        if(set->size() > max){
//            max = set->size();
//            biggest1 = set;
//        }
//        
//    }
//    
//    unsigned long max2(0); std::unordered_set<Vid>* biggest2(0);
//
//    for(auto set: *s2){
//        if(set->size() > max2){
//            max2 = set->size();
//            biggest2 = set;
//        }
//    }
//    
//    for(Vid element: *biggest1){
//        if(!biggest2->count(element))
//            dif->insert(element);
//    
//    }
//    
//    for(Vid element: *biggest2){
//        if(!biggest1->count(element))
//            dif->insert(element);
//        
//    }
//    
//    return dif;
    return 0;
}

//Returns an integer from the range [lo,hi)
int Utility::randomInt(int lo, int hi){
    
    return floor((hi - lo)*dist(engine) + lo);
}

void Utility::shuffleArray(Vid* array, Vid size){
    
    std::shuffle(array, array+size, engine);
    
}

// vertex = cluster + numClusters*x
Graph<Vid>* Utility::clusters(const int numClusters, const int clusterSize, const int numNeighbors, const int interClusterConnections){
    

    Graph<Vid>* graph = new AdjacencyListGraph<Vid>;
    int edge, x, clust;
   
    for(int cluster = 0; cluster < numClusters; cluster++){
        
        for(int v = 0; v < clusterSize; ++v){
            //Insert the vertex
            graph->insertVertex(cluster + v*numClusters);
            
            //connect the vertex to numNeighbors neighbors
            edge = 0;
            while(edge++ < numNeighbors){
                x =  Utility::randomInt(0, clusterSize);
                graph->insertEdge(cluster + v*numClusters, cluster + x*numClusters);
            }
            
        }
        
        if(cluster == numClusters-1)
            continue;
        
        //Connect the vertex to other clusters
        
        if(interClusterConnections < clusterSize){
            
            for(int j = 0; j < interClusterConnections; ++j){
                
                clust = Utility::randomInt(cluster+1, numClusters);
                int neighbor = Utility::randomInt(0, clusterSize)*numClusters +clust;
                graph->insertEdge(j*numClusters + cluster, neighbor);
                
            }
        }
        
        else{
            for(int j = 0; j < interClusterConnections; ++j){
                
                clust = Utility::randomInt(cluster+1, numClusters);
                int neighbor = Utility::randomInt(0, clusterSize)*numClusters +clust;
                graph->insertEdge((j%clusterSize)*numClusters + cluster, neighbor);
                
            }
        }
        
    

    }
    graph->updateVertexArray();
    return graph;
    
}

/*Returns directed hash graph represented by CSP files
 
 The files are of the form:
 
 vertex: neighbor1 neighbor2 neighbor3  ...
 
 Example line:
 
 10: 13 14 19
 
 This line says that the graph contains a vertex with ID 10 that has neighbors 13,14, and 19
 
 
 
 */
Graph<Vid>* Utility::importGraphFromCSP(std::string filename){
    
    std::ifstream file(filename);
    std::string line;
    
    
    if(!file.is_open()){
        std::cerr << "Issue reading file: " << filename << std::endl;
        return nullptr;
    }
    
    auto graph = new std::unordered_map<Vid, std::vector<Vid>>;
    
    std::getline(file, line); //ignore the first line
    
    //The 'garbage' variable is used to remove the colon seperating the vertex  from its neighbors
    Vid vertex, neighbor; char garbage;

    while(std::getline(file, line)){
        std::istringstream stream(line);
        
        stream >> vertex  >> garbage;
        
        std::vector<Vid>& neighbors = (*graph)[vertex];
        
        
        while(stream >> neighbor)
            neighbors.push_back(neighbor);
        

        graph->insert(std::make_pair(vertex, neighbors));

    }
    
    return new AdjacencyListGraph<Vid>(std::move(*graph));
}







