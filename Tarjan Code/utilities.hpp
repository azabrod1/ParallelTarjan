//
//  utilities.hpp
//  Tarjan
//
//  Created by Alex Zabrodskiy on 4/28/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#ifndef utilities_hpp
#define utilities_hpp


#include <stdio.h>
#include "directedHashGraph.h"
#include "typedefs.h"
#include <random>       // for pseudo-random number generators and distributions


class Utility{
    
public:
    
    static bool initialized;
    static Graph<Vid>* generateRandomGraph(double edgeProb, const Vid& size);
    static Graph<Vid>* GeoGenerateRandomGraph(double edgeProb, const double& corr, const Vid& size);
    
    static std::uniform_real_distribution<double> dist;
    static std::mt19937 engine;
    
    static std::unordered_set<Vid>* missMatches(SCC_Set* s1, SCC_Set* s2);

    static std::unordered_set<Vid>* bigDif(SCC_Set* s1, SCC_Set* s2);

    static void deleteSCCs(SCC_Set* SCCs);
    
    static int randomInt(int lo, int hi);
    
    static Graph<Vid>* clusters(const int numClusters, const int clusterSize, const int numNeighbors, const int interClusterConnections);
    
    static void shuffleArray(Vid* array, Vid size);
    
    static Graph<Vid>* importGraphFromCSP(std::string filename);
    
    
    
    
};



#endif /* utilities_hpp */
