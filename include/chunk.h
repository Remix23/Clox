#ifndef clox_chunk_h
#define clox_chunk_h

#include "common.h"
#include "value.h"

typedef enum {
    OP_RETURN,
    OP_CONSTANT,
    OP_NEGATE,

    // booleans
    OP_TRUE,
    OP_FALSE,
    OP_NIL,
    

    // Logical
    // ! Normmally we would make more operations but less complex
    // ? eg. make a OP_NOT_EQUAL, OP_LESS_EQUAL, OP_GREATER_EQUAL
    OP_EQUAL,
    OP_GREATER, 
    OP_LESS,

    OP_NOT,
    // Logical
    OP_OR,
    OP_AND,

    // Binary operations
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,

    // TODO: Add support for 
    OP_CONSTANT_LONG,

    // statements
    OP_PRINT,
    OP_POP,

    // declatartions
    OP_DEFINE_GLOBAL,
    OP_GET_GLOBAL,
    OP_SET_GLOBAL,

    // local varialbles
    OP_SET_LOCAL,
    OP_GET_LOCAL,

    // upvalues - closures
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,

    // control flow
    OP_JUMP_IF_FALSE,
    OP_JUMP,
    OP_JUMP_BACK,

    // func support
    OP_CALL, // takes as operand one byte: nr of arguments
    OP_CLOSURE,

    OP_CLOSE_CAPTURE,

    OP_CLASS,
    OP_GET_PROPERTY,
    OP_SET_PROPERTY,

    OP_METHOD,
} OpCode;

typedef struct {
    int count;
    int capacity;
    uint8_t* code;
    int* lines;
    ValueArray constants;
} Chunk;

void initChunk (Chunk* chunk);
void writeChunk (Chunk* chunk, uint8_t byte, int line);
void freeChunk (Chunk* chunk);

int addConstant (Chunk* chunk, Value value);

#endif