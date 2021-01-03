//
//  ReferenceCounter.cpp
//  Tarjan4
//
//  Created by Alex Zabrodskiy on 6/5/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#include <stdio.h>
#include "Reference.hpp"
#include "cell.h"

template<> void ReferenceCounter<Cell<Vid>>::deleter(){
    
    object->recycleThis();
}

WeakReference<Cell<Vid>> nullWeakReference = WeakReference<Cell<Vid>>(nullptr, 0);
