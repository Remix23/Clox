#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "value.h"

#define MAX_STACK 256 // grow dynamically?

typedef struct {
    Chunk* chunk;
    uint8_t* ip;
    Value stack[MAX_STACK];
    Value* stackTop;
    Obj* objects;
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