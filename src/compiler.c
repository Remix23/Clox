#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "compiler.h"
#include "common.h"
#include "chunk.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,  // =
    PREC_OR,          // or
    PREC_AND,         // and
    PREC_EQUALITY,    // == !=
    PREC_COMPARISON,  // < > <= >=
    PREC_TERM,        // + -
    PREC_FACTOR,      // * /
    PREC_UNARY,       // ! -
    PREC_CALL,        // . ()
    PREC_PRIMARY
} Precedence;


typedef void (*ParseFn) (); // function pointer to a func that returns void

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParserRule;

Parser parser;
Chunk* compilingChunk;

// ========= Parsing Declarations =========

static void expression ();
static ParserRule* getRule (TokenType type);
static void parsePrecedence (Precedence precedence);

// ========= Parsing debuging =========

static void errorAt (Token* token, const char* msg) {
    // suppress any further errors msgs
    if (parser.panicMode) return;
    
    parser.panicMode = true;
    fprintf(stderr, "[line %d] Error", token->line);

    if (token->ttype == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else {
        fprintf (stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", msg);
    parser.hadError = true;
}

static void errorAtCurrent (const char* msg) {
    errorAt(&parser.current, msg);
}

// ========= Parsing helpers =========

static Chunk* currentChunk () {
    return compilingChunk;
}

static uint8_t makeConstant (Value val) {
    int constant = addConstant(currentChunk(), val);

    // since we are suing sinle byte operands to OP_CONSTANT
    if (constant > UINT8_MAX) {
        errorAtCurrent("Too many constants in one chunk");
        return 0;
    }

    return (uint8_t) constant;
}

static void emitByte (uint8_t byte) {
    writeChunk(currentChunk(), byte, parser.previous.line);
}


static void emitBytes (uint8_t b1, uint8_t b2) {
    emitByte(b1);
    emitByte(b2);
}


static void emitConstant (Value val) {
    emitBytes(OP_CONSTANT, makeConstant(val));
}

static void emitReturn () {
    emitByte(OP_RETURN);
}

static void endCompiler () {
    emitReturn();

#ifdef DEBUG_PRINT_CODE 
    if (!parser.hadError) {
        disassembleChunk(currentChunk(), "code");
    }
#endif
}

static void advance () {
    parser.previous = parser.current;

    for (;;) {
        parser.current = scanToken();
        if (parser.current.ttype != TOKEN_ERROR) break;

        errorAtCurrent(parser.current.start);
    }
}

static void consume (TokenType type, const char* msg) {
    if (parser.current.ttype == type) {
        advance();
        return;
    }

    errorAtCurrent(msg);
}

// ========= Parsing tokens =========

static void binary () {
    TokenType opType = parser.previous.ttype;

    ParserRule* rule = getRule (opType);
    parsePrecedence((Precedence) (rule -> precedence + 1));

    switch (opType) {
        case TOKEN_PLUS: emitByte(OP_ADD); break;
        case TOKEN_MINUS: emitByte(OP_SUBTRACT); break;
        case TOKEN_STAR: emitByte(OP_MULTIPLY); break;
        case TOKEN_SLASH: emitByte(OP_DIVIDE); break;
        default: // unreachable
            return;
    }
}

static void number () {
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}

static void grouping () {
    expression ();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression");
}

static void unary () {
    TokenType opType = parser.previous.ttype;

    // compile the operand
    parsePrecedence(PREC_UNARY);

    // emit the operator funcion
    switch (opType)
    {
    case TOKEN_MINUS: emitByte(OP_NEGATE); break;
    case TOKEN_BANG: emitByte(OP_NOT); break;
    
    default:
        break;
    }
}

static void literal () {
    switch (parser.previous.ttype) {
        case TOKEN_FALSE: emitByte(OP_FALSE); break;
        case TOKEN_TRUE: emitByte(OP_TRUE); break;
        case TOKEN_NIL: emitByte(OP_NIL); break;
        default:
            break;
    }
}

static void ternary () {
    // parser.previous points to the question mark

    // parsing the true expression
    printf("Parsing ternary expression\n");
    expression();

    // consume the colon
    consume(TOKEN_COLON, "Expect ':' after then branch");

    printf("Consumed colon\n");

    // parsing the false expression
    expression();
}

static void comma () {
}

// ========= Parsing rules =========

ParserRule rules [] = {
    [TOKEN_LEFT_PAREN] = {grouping, NULL, PREC_NONE},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},

    // colons and dots
    [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
    [TOKEN_DOT] = {NULL, NULL, PREC_NONE},
    [TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE},
    [TOKEN_COLON] = {NULL, NULL, PREC_NONE},
    
    // add question mark - ternany operator
    [TOKEN_QUESTION_MARK] = {NULL, ternary, PREC_ASSIGNMENT},

    // binary operators
    [TOKEN_MINUS] = {unary, binary, PREC_TERM},
    [TOKEN_PLUS] = {NULL, binary, PREC_TERM},
    [TOKEN_STAR] = {NULL, binary, PREC_FACTOR},
    [TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},

    // the rest
    [TOKEN_BANG] = {unary, NULL, PREC_NONE},
    [TOKEN_BANG_EQUAL] = {NULL, NULL, PREC_NONE},

    [TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},

    // comparisons
    [TOKEN_EQUAL_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_LESS] = {NULL, NULL, PREC_NONE},
    [TOKEN_LESS_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_GREATER] = {NULL, NULL, PREC_NONE},
    [TOKEN_GREATER_EQUAL] = {NULL, NULL, PREC_NONE},
    
    // literals
    [TOKEN_IDENTIFIER] = {NULL, NULL, PREC_NONE},
    [TOKEN_STRING] = {NULL, NULL, PREC_NONE},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE},

    // binary
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE},
    [TOKEN_TRUE] = {literal, NULL, PREC_NONE},
    [TOKEN_NIL] = {literal, NULL, PREC_NONE},
    
    // binary operands
    [TOKEN_AND] = {NULL, NULL, PREC_NONE},
    [TOKEN_OR] = {NULL, NULL, PREC_NONE},

    // control flow 
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
    [TOKEN_IF] = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},

    [TOKEN_CONTINUE] = {NULL, NULL, PREC_NONE},
    [TOKEN_BREAK] = {NULL, NULL, PREC_NONE},

    // declatations
    [TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
    [TOKEN_SUPER] = {NULL, NULL, PREC_NONE},
    [TOKEN_THIS] = {NULL, NULL, PREC_NONE},

    [TOKEN_FUN] = {NULL, NULL, PREC_NONE},
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},

    // print 
    [TOKEN_PRINT] = {NULL, NULL, PREC_NONE},

    [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF] = {NULL, NULL, PREC_NONE}
};

static ParserRule* getRule (TokenType type) {
    return &rules[type];
}

// ========= Parsing expressions =========

static void expression () {
    parsePrecedence(PREC_ASSIGNMENT);
}

static void parsePrecedence (Precedence precedence) {
    advance ();

    ParseFn prefixRule = getRule(parser.previous.ttype) -> prefix;

    if (prefixRule == NULL) {
        errorAtCurrent("Expect expression");
        return;
    }

    prefixRule();

    while (precedence <= getRule(parser.current.ttype) -> precedence) {
        advance ();
        ParseFn infixRule = getRule(parser.previous.ttype) -> infix;

        if (infixRule == NULL) {
            errorAtCurrent("Not good");
            return;
        }

        infixRule();
    }
}

bool compile (const char* source, Chunk* chunk) {
    initScanner(source);
    compilingChunk = chunk;

    parser.hadError = false;
    parser.panicMode = false;

    advance();   
    expression();
    consume (TOKEN_EOF, "Expect end of expression");

    endCompiler();

    return !parser.hadError;
}
