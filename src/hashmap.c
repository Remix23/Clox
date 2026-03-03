#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "hashmap.h"
#include "memory.h"
#include "object.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

static Entry* findEntry (Entry* entries, int capacity, ObjString* key) {
    uint32_t index = key -> hash % (capacity - 1);

    Entry* tombstone = NULL;

    for (;;) {
        Entry* entry = &entries[index];

        if (entry -> key == NULL) {
            if (IS_NIL(entry -> value)) {
                return tombstone != NULL ? tombstone : entry;
            } else {
                if (tombstone == NULL) tombstone = entry;
            }
        } else if (entry -> key == key) {
            return entry;

        }

        index = (index + 1) % (capacity - 1);
    }
}

static void adjustCapacity (HashMap* map, int capacity) {
    Entry* entries = ALLOCATE(Entry, capacity);

    for (int i = 0; i < capacity; i++) {
        entries[i].key = NULL;
        entries[i].value = NIL_VAL;
    }

    map -> count = 0;

    for (int i = 0; i < map -> capacity; i++) {
        Entry* entry = &map -> entries[i];
        if (entry -> key == NULL) continue;

        Entry* dest = findEntry(entries, capacity, entry -> key);
        dest -> key = entry -> key;
        dest -> value = entry -> value;
        map -> count++;
    }

    free(map -> entries);
    map -> entries = entries;
    map -> capacity = capacity;
}

void initHashMap (HashMap* map) {
    map->capacity = 0;
    map->count = 0;
    map->entries = NULL;
}

bool hashMapSet (HashMap* map, ObjString* key, Value value) {
    if (map -> count + 1 > map -> capacity * TABLE_MAX_LOAD) {
        int capacity = GROW_CAPACITY(map -> capacity);

        adjustCapacity(map, capacity);
    }

    Entry* entry = findEntry(map -> entries, map -> capacity, key);

    bool isNewKey = entry -> key == NULL;

    if (isNewKey) map -> count++;
    if (isNewKey && IS_NIL(entry -> value)) map -> count++;

    entry -> key = key;
    entry -> value = value;
    return isNewKey;
}

bool hashMapGet (HashMap* map, ObjString* key, Value* val) {
    if (map -> count == 0) return false;

    Entry* entry = findEntry(map -> entries, map -> capacity, key);
    if (entry -> key == NULL) return false;


    *val = entry ->value;
    return true;
}

void hashMapAddAll (HashMap* from, HashMap* to) {
    for (int i = 0; i < from -> capacity; i++) {
        Entry* entry = &from -> entries[i];
        if (entry -> key != NULL) {
            hashMapSet(to, entry -> key, entry -> value);
        }
    }
}

bool hashMapDelete (HashMap* map, ObjString* key) {
    if (map -> count == 0) return false;

    // find the entry
    Entry* entry = findEntry(map -> entries, map -> capacity, key);
    if (entry -> key == NULL) return false;

    // place a tombstone in the entry
    entry -> key = NULL;
    entry -> value = BOOL_VAL(true);

    return true;
}

ObjString* hashMapFindString (HashMap* map, const char* chars, int lenght, uint32_t hash) {
    if (map -> count == 0) return NULL;

    uint32_t index = hash % (map -> capacity - 1);

    for (;;) {
        Entry* entry = &map -> entries[index];

        if (entry -> key == NULL) {
            if (IS_NIL(entry -> value)) return NULL;
        } else if (entry -> key -> length == lenght &&
                entry -> key -> hash == hash &&
                memcmp(entry -> key -> chars, chars, lenght) == 0) {
            return entry -> key;
        }

        index = (index + 1) % (map -> capacity - 1);
    }
}

void markHashMap (HashMap* map) {
    for (int i = 0; i < map->capacity; i++) {
        Entry* entry = &map->entries[i];
        markObject((void*) entry->key);
        markValue(entry->value);
    }
}

void hashMapRemoveWhite (HashMap* map) {
    for (int i = 0; i < map->capacity; i ++) {
        Entry* entry = &map->entries[i];
        if (entry->key != NULL && !entry->key->obj.isMarked) {
            hashMapDelete(map, entry->key);
        }
    }
}

void freeHashMap (HashMap* map) {
    FREE_ARRAY(Entry, map -> entries, map -> capacity);
    initHashMap(map);
}
