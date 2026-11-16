#pragma once
#ifndef BOONTA_RNBO_HEAP_H
#define BOONTA_RNBO_HEAP_H

#include <new>
#include <stddef.h>

/** Where an RNBO patch's memory comes from. See RnboHeap.cpp for why none of it
 *  can come from where the compiler would have put it. */
namespace rnbo_heap
{
/** A patcher object, from the RAM_D2 pool. Null when the pool is exhausted,
 *  which the caller must treat as "this effect cannot run" rather than pressing
 *  on -- an RNBO patcher on a bad pointer faults on its first block. */
void* AllocatePatcher(size_t size, size_t align);

template <class T>
T* NewPatcher()
{
    void* mem = AllocatePatcher(sizeof(T), alignof(T));
    return mem ? new(mem) T() : nullptr;
}

} // namespace rnbo_heap

#endif
