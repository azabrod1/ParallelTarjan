//
//  search.cpp
//  Tarjan
//
//  Created by Alex Zabrodskiy on 4/26/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#include "search.hpp"
#include "multiThreadedTarjan.hpp"
#include <algorithm>    // std::max

                              /*******************   Constructor and Destructor   ************/


Search::Search() : cellCount(0), age(0), cellBlockedOn(nullptr){
}

Search::~Search(){ }

void Search::setRoot(Cell<Vid>* root){
    pushToStacks(root);
}

/**
 Pre:  The cell must be owned by the thread
 Post: The cell is added to both the search's tarjan and control stack.
       Additionally, the cell is given an index and we increment cellCount
 
 @param newCell Pointer to the cell we are pushing to our stacks
 */
void Search::pushToStacks(Cell<Vid>* newCell){
    tarjanStack.push_back(newCell); controlStack.push_back(newCell);
    newCell->initIndex(cellCount++);
}

//When the search receives new cells as part of a transfer, the cellCount should be updated to reflect this
void Search::refreshCellCount(){
    cellCount = tarjanStack.back()->index + 1;
}

                        /************* Methods for node transfer ***************/

/**
 Pre:    The searches S0,S1, S2, S3,...,Sn are in a blocking cycle such that
         So --> S1 -->  S2 --> S3 -->...--> Sn
         Cells are being transferred from S0 to Sn-1 to Sn to resolve the cycle
 
 Inputs: src is one of the searches Si from S0 to Sn-1
         dest is the search Sn that is recieving the transferred cell
         conflict cell is the cell of src the search before src, S_{i-1} is blocked on and
         corresponds to the cell Ci in the paper
 
 Purpose: First, The function transfers cells from the tarjanStack of src to the tarjanStack of dest.
          Cell transfer starts at the top of src's tarjanStack and proceeds until the following two conditons are met
          a) The conflict cell is transferred to dest
          b) The last cell transferred has rank no greater than the minimum rank of the cells transferred so far
 
          ...a) is important because we want to transfer the cell causing the conflict to dest so that the conflict is 
          resolved. b) is important because we want to transfer all the cells in the same SCC as the conflict cell
          otherwise dest and src will both have some cells in that SCC on their stack, and hence they will again collide
          and form a cycle
 
          defining L to be the is the last cell we transferred from src's tarjanStack,
          we transfer all the cells in src's control stack starting from its top and 
          proceeding until we reach L. Note that L must be on src's controlStack. To see why, note that
          L.index must equal L.rank otherwise it would be impossible for L to have index lower than 
          any rank encountered so far since its own index will be less than its rank.
 
 Post:    The relevant cells discussed above are tranferred to dest with their index and rank variables updated to
          fit with their new stack. The search src is now blocked on the last cell transferred to dest
 
 */


        /*Transfer cells from source to destination */


void Search::transferCells(Search* const src, Search* const dest, Cell<Vid>* const conflictCell){

    TarjanStack &  srcTarjanStack  = src->tarjanStack;
    ControlStack& srcControlStack  = src->controlStack;
    
    //We use a reverse iterator since we are starting from the top of the stack
    auto reverseIt = srcTarjanStack.rbegin();
    
    Cell<Vid>* next = *reverseIt; Vid minRank(next->rank);
    bool reachedCC(next == conflictCell);
    

    /* Remove cells from src's tarjanStack, starting from the top and proceeding until
     a) The conflict cell is transferred to dest
     b) The latest cell transferred has rank no greater than the minimum rank of the cells transferred so far
     */
    
    while(!reachedCC || next->index > minRank){
        next = *(++reverseIt); //Increment iterator and get the next element
        minRank = std::min(minRank, next->rank);
        if(next == conflictCell) reachedCC = true;
    }
    
    //Transfer the relevent cells from src's tarjan stack to dest's tarjan stack
    
    Cell<Vid>* const last(next);
    
    Vid delta(dest->cellCount - last->index);
    
    //Carry out the transfer
    dest->tarjanStack.insert(dest->tarjanStack.end(), (reverseIt+1).base(), srcTarjanStack.end());
    
    //adjust the index and ranks and status of the transferred cells
    //to be compatible with the cell's new tarjanStack
    for(auto it = (reverseIt+1).base(); it != srcTarjanStack.end(); ++it)
        (*it)->transfer(delta, dest);
    
    //Erase the cells transferred to dest from src's tarjan stack
    //Using stl functions to accomplish this may be faster than a loop hence we use the
    //erase() function
    srcTarjanStack.erase((reverseIt+1).base(), srcTarjanStack.end());
    
    auto rit = srcControlStack.rbegin();
    
    while(*rit != last) ++rit;
    
    //Transfer cells from src control stack to destination control stack
    //starting from the controlStack's top and proceeding to the last cell transferred
    
    dest->controlStack.insert(dest->controlStack.end(), (rit+1).base(), srcControlStack.end());
    
    srcControlStack.erase((rit+1).base(), srcControlStack.end());
    
    
    //transfered to dest
    //Src now blocks on the deepest cell in the tarjan stack that we transfered to dest
    if(!srcTarjanStack.empty()){
        src->suspendOn(last);
        last->blockSearch(src);
        src->refreshCellCount();
    }
    else
        src->removeCellBlockedOn();
    
    //CellCount of dest must be adjusted as well since we added more cells 
    dest->refreshCellCount();
    
}


void Search::printStacks(){
    
    std::cout<< "Tarjan stack " <<std::endl;
    
    for(Cell<Vid>* cell: tarjanStack)
        std::cout << cell->index << " ";
    
    std::cout<< std::endl;
    
    std::cout<< "control stack " <<std::endl;
    
    for(Cell<Vid>* cell: controlStack)
        std::cout << cell->index << " ";
    
    std::cout << "\ncell count: " << this->cellCount;
    
    std::cout<< std::endl;
    
    
}




