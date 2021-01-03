//
//  typedefs.cpp
//  Tarjan
//
//  Created by Alex Zabrodskiy on 4/30/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#include <stdio.h>
#include "typedefs.h"


/*
long const CellStatus::dummyComplete[] = {1,1,1,1,1,1,1,1,1};
CellStatus* const CellStatus::COMPLETE_CELL = (CellStatus*) dummyComplete;
CellStatus* const CellStatus::NEW_CELL      = nullptr;
*/

const char CellStatus::COMPLETE  = 2;

const char CellStatus::CLAIMED = 1;

const char CellStatus::OCCUPIED  = 0;

long CellStatus::complete[100];

Search* const CellStatus::NEW_CELL        = nullptr;
Search*       CellStatus::UNCLAIMED_CELL  =  CellStatus::NEW_CELL;
Search* const CellStatus::COMPLETE_CELL((Search *) &complete);


int qCount = 0; int qCount2(0);

std::atomic<int> s(0), s2(0);

SimpleClock profiler;


 

