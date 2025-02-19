#include <stdio.h>
#include <stdlib.h>

#include "compiler.h"
#include "common.h"
#include "scanner.h"

void compile (const char* source) {
    initScanner(source);

    int line = -1;

    for (;;) {
        Token token = scanToken();
        if (token.line != line) {
            printf("%4d ", token.line);
            line = token.line;
        } else {
            printf("   | ");
        }
        printf("%2d '%.*s'\n", token.ttype, token.length, token.start);

        if (token.ttype == TOKEN_EOF) break;
    }
}