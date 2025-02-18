#include <stdlib.h>
#include <stdio.h>

#include "chunk.h"
#include "memory.h"
#include "value.h"

void initValueArray (ValueArray* arr) {
    arr->capacity = 0;
    arr->count = 0;
    arr->values = NULL;
}

void writeValueArray (ValueArray* arr, Value value) {
    if (arr->capacity < arr->count + 1) {
        int oldCapacity = arr->capacity;
        arr->capacity = GROW_CAPACITY(oldCapacity);
        arr->values = GROW_ARRAY(Value, arr->values, oldCapacity, arr->capacity);
    }

    arr->values[arr->count] = value;
    arr->count++;
}

void freeValueArray (ValueArray* arr) {
    FREE_ARRAY(Value, arr->values, arr->capacity);
    initValueArray(arr);
}

void printValue (Value value) {
    printf("%g", value);
}