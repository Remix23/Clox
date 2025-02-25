#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "chunk.h"
#include "value.h"

#define OBJ_TYPE(value) (AS_OBJ(value) -> otype)

#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_STRING(value) isObjType(value, OBJ_STRING)
#define IS_NATIVE(value) isObjType(value, OBJ_NATIVE)

#define AS_STRING(value) ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value)) -> chars)


#define AS_FUNCTION(value) ((ObjFunction*)AS_OBJ(value))
#define AS_NATIVE(value) (((ObjNativeFn*) AS_OBJ(value)) ->func)

typedef enum {
    OBJ_STRING,
    OBJ_FUNCTION,
    OBJ_NATIVE,
} ObjType;

struct Obj {
    ObjType otype;
    struct Obj* next; // linked list for the vm to clean up
};

struct ObjString {
    Obj obj;
    int length;
    char* chars;
    uint32_t hash;
};

typedef struct {
    Obj obj;
    int arity;
    Chunk chunk;
    ObjString* name;
} ObjFunction;

typedef Value (*NativeFn) (int argCount, Value* args);

typedef struct {
    Obj obj;
    NativeFn func;
} ObjNativeFn;

static inline bool isObjType(Value value, ObjType otype) {
    return IS_OBJ(value) && AS_OBJ(value) -> otype == otype;
}

ObjString* copyString(const char* chars, int lenght);
ObjString* takeString(char* chars, int lenght);

ObjFunction* newFunction();
ObjNativeFn* newNative(NativeFn cfunc);

void printObject(Value value);

#endif

// i++ != ++i 

// i++ -> i = i + 1, i - 1
// ++i -> i = i + 1, i