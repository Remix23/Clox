#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "vm.h"

#include "test.h"

static char* readFile (const char* path) {
    FILE* file = fopen(path, "rb");

    if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }

    fseek(file, 0, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(fileSize + 1); // +1 for the terminating null byte
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    
    // read error
    if (bytesRead < fileSize) {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        exit(74);
    }
    
    buffer[bytesRead] = '\0';

    fclose(file);
    return buffer;
}

// swith later to dynamic repl
static void repl () {
    char line [1024];
    for (;;) {
        printf (">> ");

        if (!fgets(line, sizeof (line), stdin)) {
            printf("\n");
            break;
        }

        if (line[0] == 'q') {
            break;
        }

        interpret(line);
    }
}

static void runFile (const char* path) {
    char* source = readFile (path);
    InterpretResult result = interpret(source);
    free(source);

    if (result == INTERPRET_COMPILE_ERROR) exit(64);
    if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}

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
    
    
    if (argc == 1) {
        repl();
    } else if (argc == 2) {
        runFile (argv[1]);
    } else {
        fprintf(stderr, "Usage: clox [path]\n");
        exit(64);
    }

    freeVM();

    return 0;
}