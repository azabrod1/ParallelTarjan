//
//  singleThreadedTarjan.cpp
//  Tarjan
//
//  Created by Alex Zabrodskiy on 4/28/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#include <stdio.h>
#include <iostream>
#include "singleThreadedTarjan.h"

//   **** Iterative Single threaded Tarjan's algorithm implementation

const char SingleCell::NEW_CELL = 0, SingleCell::ON_STACK = 1, SingleCell::COMPLETE = 2;

SCC_Set* SingleThreadedTarjan::run(){

    Vid size, vertex;
    Vid* verts = graph.getVerticesArray(size);

    for(int v = 0; v < size; ++v){
        vertex =  verts[v];
        if(!lookup.count(vertex)){
            SingleCell* root = &lookup[vertex];
            root->vertex = vertex;

            search(root);
        }
    }

    return SCCs;
}


void SingleThreadedTarjan::search(SingleCell* root){
    SingleCell* curr, * child;
    conquer(root); //Add the root of the search to the stack
    
    while(!controlStack.empty()){
        
        curr = controlStack.back();
        
        //Curr might have more neighbors not yet assigned to an SCC
        if(!curr->unassignedNeighbors.empty()){
            child = curr->unassignedNeighbors.back();
            curr->unassignedNeighbors.pop_back(); //Remove that child from list of neighbors left to inspect
            
            if(child->status == SingleCell::ON_STACK)
                curr->updateRank(child->index);
            
            else if(!child->complete()) //Not yet on stack nor assigned to an SCC
                conquer(child);
            
        }
        
        //curr has no more neighbors left to explore
        else{
            controlStack.pop_back();
            
            //Update rank of the node that linked to curr
            if(!controlStack.empty())
                controlStack.back()->updateRank(curr->rank);
            
            if(curr->rank == curr->index){
                auto scc = new SCC;
                SingleCell* cell = NULL;
                
                do{
                    cell = tarjanStack.back();
                    tarjanStack.pop_back();
                    cell->status = SingleCell::COMPLETE;
                    scc->push_back(cell->vertex);
                }while(cell != curr);
                
                SCCs->push_back(scc);
                
            }
            
        }
        
    }
}

//Delete all the nodes we created to assist our search
SingleThreadedTarjan::~SingleThreadedTarjan(){ }

