#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "chunk.h"
#include "value.h"

#include <stdbool.h>

#define OBJ_TYPE(value) (AS_OBJ(value) -> otype)

#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_STRING(value) isObjType(value, OBJ_STRING)
#define IS_NATIVE(value) isObjType(value, OBJ_NATIVE)
#define IS_CLOSURE(value) isObjType(value, OBJ_CLOSURE)
#define IS_UPVALUE(value) isObjType(value, OBJ_UPVALUE)

#define AS_STRING(value) ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value)) -> chars)

#define AS_FUNCTION(value) ((ObjFunction*)AS_OBJ(value))
#define AS_CFUNCTION(value) (((ObjNativeFn*) AS_OBJ(value)) ->func)
#define AS_NATIVE(value) ((ObjNativeFn*) AS_OBJ(value))

#define AS_CLOSURE(value) ((ObjClosure*) AS_OBJ(value))

#define AS_UPVALUE(value) ((ObjUpvalue*) AS_OBJ(value))
typedef enum {
    OBJ_STRING,
    OBJ_FUNCTION,
    OBJ_NATIVE,
    OBJ_CLOSURE,
    OBJ_UPVALUE,
} ObjType;

struct Obj {
    ObjType otype;
    bool isMarked;
    struct Obj* next; // linked list for the vm to clean up
};

struct ObjString {
    Obj obj;
    int length;
    char* chars;
    uint32_t hash;
};

typedef struct ObjUpvalue {
    Obj obj;
    Value* location;
    Value closed;
    struct ObjUpvalue* next;
} ObjUpvalue;

typedef struct {
    Obj obj;
    int arity;
    int upValuesCount;
    Chunk chunk;
    ObjString* name;
} ObjFunction;

typedef struct {
    Obj obj;
    ObjFunction* rawFunc;
    ObjUpvalue** upvalues;
    int upvalueCount;
} ObjClosure;


typedef Value (*NativeFn) (int argCount, Value* args);

typedef struct {
    Obj obj;
    int arity;
    NativeFn func;
} ObjNativeFn;

static inline bool isObjType(Value value, ObjType otype) {
    return IS_OBJ(value) && AS_OBJ(value) -> otype == otype;
}

ObjString* copyString(const char* chars, int lenght);
ObjString* takeString(char* chars, int lenght);

ObjFunction* newFunction();
ObjNativeFn* newNative(int arity, NativeFn cfunc);
ObjClosure* newClosure(ObjFunction* func);

ObjUpvalue* newUpvalue (Value* slot);

void printObject(Value value);

#endif

// i++ != ++i 

// i++ -> i = i + 1, i - 1
// ++i -> i = i + 1, i