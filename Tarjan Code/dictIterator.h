//
//  SimpleIterator.h
//  Tarjan
//
//  Created by Alex Zabrodskiy on 5/1/17.
//  Copyright © 2017 Alex Zabrodskiy. All rights reserved.
//

#ifndef simpleIterator_h
#define simpleIterator_h

#include <iostream>
#include <iterator>

template <class K, class V>
class DictIterator : public std::iterator<std::random_access_iterator_tag,
std::pair<K,V>,
ptrdiff_t,
std::pair<K,V>*,
std::pair<K,V>&>
{
    
    virtual DictIterator<K,V>& operator++()  = 0;
    
    virtual bool operator==(const DictIterator<K,V>& it) = 0;
    
    virtual bool operator!=(const DictIterator<K,V>& it) = 0;
    
    virtual std::pair<K,V>& operator*() = 0;
    
    virtual ptrdiff_t  operator-(const DictIterator<K,V>& it){return std::distance(it.getPtr(),this->getPtr());}
    
};


#endif /* SimpleIterator_h */
