//
//  typedefs.h
//  Tarjan
//
//  Created by Alex Zabrodskiy on 4/28/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#ifndef typedefs_h
#define typedefs_h

#include <unordered_set>
#include <atomic>
#include <assert.h>     /* assert */
#include <thread>
#include <memory>
#include "simpleClock.h"
#include "blockedList.hpp"

class Search; class Worker;

template <class V>
class Cell;

typedef unsigned int Vid; //Identifier for vertexes in graph

typedef std::vector<Vid> SCC;
typedef std::vector<SCC*> SCC_Set;

class Search;


extern int qCount;
extern int qCount2;
extern SimpleClock profiler;
extern std::atomic<int> s;
extern std::atomic<int> s2;

namespace CellStatus{
    
    extern long complete[];
    extern Search* const COMPLETE_CELL;
    extern Search* const NEW_CELL;
    extern Search* UNCLAIMED_CELL;
    
    extern const char COMPLETE; // = 2;
    
    extern const char CLAIMED;// = 1;
    
    extern const char OCCUPIED; //= 0;
}






#endif /* typedefs_h */
