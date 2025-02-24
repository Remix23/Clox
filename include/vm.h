#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "value.h"
#include "object.h"
#include "memory.h"
#include "hashmap.h"

#define MAX_FRAMES 64
#define MAX_STACK (MAX_FRAMES * UINT8_COUNT) // grow dynamically?

typedef struct {
    uint8_t* ip; // the return adress
    Value* slots; // pointer to the first slot in the stack the callframe can use
    ObjFunction* function;
} CallFrame;
typedef struct {
    CallFrame frames [MAX_FRAMES];
    int frameCount;

    Value stack[MAX_STACK];
    Value* stackTop;
    HashMap strings; // interned strings
    Obj* objects;
    HashMap globals;
} VM;



typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;

extern VM vm;

void initVM ();
void freeVM ();

InterpretResult interpret (const char* source);
void push (Value value);
Value pop ();

#endif