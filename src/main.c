#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "vm.h"

#include "test.h"

// Custom includes
int main (int argc, char *argv[]) {

    // testing
    if (argc > 1) {
        if (strcmp(argv[1], "test") == 0) {
            testerRunner("tests");
            return 0;
        }
    }

    initVM();

    Chunk chunk;
    initChunk(&chunk);
    
    int constant = addConstant(&chunk, 1.2);
    writeChunk(&chunk, OP_CONSTANT, 123);
    writeChunk(&chunk, constant, 123);
    
    writeChunk(&chunk, OP_RETURN, 123);
    
    disassembleChunk(&chunk, "test chunk");

    interpret(&chunk);

    freeVM();
    freeChunk(&chunk);

    return 0;
}