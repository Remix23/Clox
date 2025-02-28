#ifndef clox_memory_h 
#define clox_memory_h

#include "common.h"

#define ALLOCATE(type, count) \
    (type*)reallocate(NULL, 0, sizeof(type) * (count))

// size (here 8) is up to define - depending on the tradeoff between memory and speed
#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : capacity * 2)

#define GROW_ARRAY(type, pointer, oldCount, newCount) \
    (type*)reallocate(pointer, sizeof(type) * (oldCount), \
    sizeof(type) * (newCount))

#define FREE_ARRAY(type, pointer, oldCount) \
    reallocate(pointer, sizeof(type) * (oldCount), 0)

#define FREE(type, pointer) \
    reallocate(pointer, sizeof(type), 0)

#ifdef DEBUG_LOG_GC

#include <stdio.h>
#include "debug.h"
#endif

void* reallocate (void* pointer, size_t oldSize, size_t newSize);
void freeObjects();

void collectGarbage();
void markValue (Value value);
void markObject (Obj* obj);

#endif