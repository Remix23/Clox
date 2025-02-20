#ifndef clox_value_h
#define clox_value_h

#include "common.h"
#include <stdbool.h>

typedef enum {
    VAL_BOOL,
    VAL_NIL,
    VAL_NUMBER,
    VAL_OBJ,
} ValueType;

typedef struct Obj Obj;
typedef struct ObjString ObjString;

typedef struct {
    ValueType vtype;
    union {
        bool boolean;
        double number;
        Obj* obj;
    } as;
} Value;

// macros: C -> lox
#define NIL_VAL ((Value) {VAL_NIL, {.number = 0}})
#define BOOL_VAL(value) ((Value) {VAL_BOOL, {.boolean = value}})
#define NUMBER_VAL(value) ((Value) {VAL_NUMBER, {.number = value}})
#define OBJ_VAL(object) ((Value) {VAL_OBJ, {.obj = (Obj*)object}})

// macros: lox -> C
#define AS_BOOL(value) ((value).as.boolean)
#define AS_NUMBER(value) ((value).as.number)
#define AS_OBJ(value) ((value).as.obj) // returns a pointer

// macros: guards
#define IS_BOOL(value) ((value).vtype == VAL_BOOL)
#define IS_NIL(value) ((value).vtype == VAL_NIL)
#define IS_NUMBER(value) ((value).vtype == VAL_NUMBER)
#define IS_OBJ(value) ((value).vtype == VAL_OBJ)

typedef struct
{
    int capacity;
    int count;
    Value* values;
} ValueArray;

void initValueArray (ValueArray* array);
void writeValueArray (ValueArray* array, Value value);
void freeValueArray (ValueArray* array);
void printValue (Value value);

bool valuesEqual (Value a, Value b);

#endif