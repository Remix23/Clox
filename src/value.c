#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "chunk.h"
#include "memory.h"
#include "value.h"
#include "object.h"

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
    switch (value.vtype)
    {
    case VAL_BOOL:
        printf(value.as.boolean ? "true" : "false");
        break;
    case VAL_NIL: printf("nil"); break;
    case VAL_NUMBER: printf("%g", value.as.number); break;
    case VAL_OBJ: printObject(value); break;
    default:
        break;
    }
}

bool valuesEqual (Value a, Value b) {
    if (a.vtype != b.vtype) return false;

    switch (a.vtype) {
        case VAL_BOOL: return a.as.boolean == b.as.boolean;
        case VAL_NIL: return true;
        case VAL_NUMBER: return a.as.number == b.as.number;
        case VAL_OBJ: {
            ObjString* aString = AS_STRING(a);
            ObjString* bString = AS_STRING(b);
            if (aString->length != bString->length) return false;

            return memcmp(aString->chars, bString->chars, aString->length) == 0;
        }
        default: return false; // unreachable
    }
}