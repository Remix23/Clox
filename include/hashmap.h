#ifndef clox_hashmap_h
#define clox_hashmap_h

#define INITIAL_CAPACITY 8

#include <stdbool.h>
#include "object.h"

typedef struct {
    ObjString* key;
    Value value;
} Entry;

typedef struct {
    int capacity;
    int count;
    Entry* entries;

} HashMap;

void initHashMap(HashMap* map, int init_size);

void freeHashMap (HashMap *map);

bool hashMapSet (HashMap* map, ObjString* key, Value value);
bool hashMapGet (HashMap* map, ObjString* key, Value* val);
bool hashMapDelete (HashMap* map, ObjString* key);
void hashMapAddAll (HashMap* from, HashMap* to);
void markHashMap (HashMap* map);
void hashMapRemoveWhite (HashMap* map);

ObjString* hashMapFindString (HashMap* map, const char* chars, int length, uint32_t hash);

#endif