zpp::allocator
==============

Introduction
------------

A simple allocator allowing dynamic memory allocation from a flat memory region.

Usage:
```cpp
#include "zpp_allocator.h"
#include <iostream>
#include <string>
#include <vector>

namespace zpp
{
template <typename Type>
using vector = std::vector<Type, zpp::static_allocator<Type>>;
using string = std::basic_string<char,
                                 std::char_traits<char>,
                                 zpp::static_allocator<char>>;
}

int main()
{
    std::byte memory[0x1000];
    zpp::heap<>::create(memory, sizeof(memory));

    auto & allocator = zpp::heap<>::get_allocator();
    std::cout << "Allocated: " << allocator.allocated() << '\n';

    zpp::vector<zpp::string> v = {"Hello", "World"};

    std::cout << "Allocated: " << allocator.allocated() << '\n';

    v.push_back("How are you!");

    std::cout << "Allocated: " << allocator.allocated() << '\n';

    v.clear();
    v.shrink_to_fit();

    std::cout << "Allocated: " << allocator.allocated() << '\n';
    return 0;
}
```

Current Features
----------------
In the example above we use the `zpp::static_allocator` class which represents
a stateless, "always equal" allocator that allocates bytes from the global heap `zpp::heap<>`.
```cpp
static_assert(std::allocator_traits<
              zpp::static_allocator<std::byte>>::is_always_equal::value);
```

In addition to static allocators, the library provides stateful allocators named
`zpp::allocator<Type>` that can be constructed from a memory region by poitner and size.

It is possible to have multiple heaps in one program, by sending the heap index to
`zpp::heap<Index>` where `zpp::heap<> == zpp::heap<0>` and `zpp::heap<1>, zpp::heap<2>, ...` each
represent a distinct heap.

You can use a different heap with `zpp::static_allocator` like so: `zpp::static_allocator<std::byte, zpp::heap<1337>>`. Remember
however to create the heap beforehand: `zpp::heap<1337>::create(pointer, size)`.

Important Notes
---------------
1. Currently the allocators provided by the framework do not throw any exception
on allocation failure. The intent is that this framework would be used only
in places where it is not possible to use memory allocations / exceptions.
2. Currently the allocator is not locking any mutex, to remove dependencies
when this is not needed. It is not recommended to use in multi threaded environment
unless using a separate heap for each thread.
