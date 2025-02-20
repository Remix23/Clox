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
    OP_NOT,

    // Binary operations
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,

    // TODO: Add support for 
    OP_CONSTANT_LONG,
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