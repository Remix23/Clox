#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "compiler.h"
#include "common.h"
#include "chunk.h"
#include "scanner.h"
#include "object.h"
#include "hashmap.h"

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


typedef void (*ParseFn) (bool canAssign); // function pointer to a func that returns void

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParserRule;

Parser parser;
Chunk* compilingChunk;

// ========= Parsing Declarations =========

static void expression ();
static void statement ();
static void declaration ();
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

static bool check (TokenType expected) {
    return parser.current.ttype == expected;
} 

static bool match (TokenType expected) {
    if (!check(expected))  return false;

    advance();
    return true;
}

static inline TokenType peek () {
    return parser.current.ttype;
}

static uint8_t identifierConstant (Token* name) {
    return makeConstant(OBJ_VAL(copyString(name -> start,
                                           name -> length)));
}

static void defineVariable (uint8_t global) {
    emitBytes(OP_DEFINE_GLOBAL, global); // global is the index in the constant table
}

static void namedVariable (Token name, bool canAssign) {
    uint8_t arg = identifierConstant(&name); // store the identifier as a string in te hash table
    

    if (match(TOKEN_EQUAL) && canAssign) {
        expression();
        emitBytes(OP_SET_GLOBAL, arg);
    } else {
        emitBytes(OP_GET_GLOBAL, arg);
    }
}


static void synchonize () {
    parser.panicMode = false;

    // skip tokens until we reach a semicolon

    while (parser.current.ttype != TOKEN_EOF) {
        if (parser.previous.ttype == TOKEN_SEMICOLON) return;

        // declaration statemets
        switch (parser.previous.ttype) {
            case TOKEN_CLASS:
            case TOKEN_FUN:
            case TOKEN_VAR:
            case TOKEN_FOR:
            case TOKEN_WHILE:
            case TOKEN_IF:
            case TOKEN_PRINT:
            case TOKEN_CONTINUE:
            case TOKEN_BREAK:
            case TOKEN_RETURN:
                return;

            default:
               ; // do nothing
        }
        advance();

    }
}

// ========= Parsing tokens =========

static void binary (bool canAssign) {
    TokenType opType = parser.previous.ttype;

    ParserRule* rule = getRule (opType);
    parsePrecedence((Precedence) (rule -> precedence + 1));

    switch (opType) {
        case TOKEN_PLUS: emitByte(OP_ADD); break;
        case TOKEN_MINUS: emitByte(OP_SUBTRACT); break;
        case TOKEN_STAR: emitByte(OP_MULTIPLY); break;
        case TOKEN_SLASH: emitByte(OP_DIVIDE); break;

        case TOKEN_EQUAL_EQUAL: emitByte(OP_EQUAL); break;
        case TOKEN_BANG_EQUAL: emitByte(OP_EQUAL); emitByte(OP_NOT); break;
        
        case TOKEN_LESS: emitByte(OP_LESS); break;
        case TOKEN_LESS_EQUAL: emitByte(OP_GREATER); emitByte(OP_NOT); break;

        case TOKEN_GREATER: emitByte(OP_GREATER); break;
        case TOKEN_GREATER_EQUAL: emitByte(OP_LESS); emitByte(OP_NOT); break;
        
        default: // unreachable
            return;
    }
}

static void number (bool canAssign) {
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}

static void string (bool canAssign) {
    Value val = OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2));
    emitConstant(val);
}

static void grouping (bool canAssign) {
    expression ();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression");
}

static void unary (bool canAssign) {
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

static void literal (bool canAssign) {
    switch (parser.previous.ttype) {
        case TOKEN_FALSE: emitByte(OP_FALSE); break;
        case TOKEN_TRUE: emitByte(OP_TRUE); break;
        case TOKEN_NIL: emitByte(OP_NIL); break;
        default:
            break;
    }
}

static void ternary (bool canAssign) {
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

static void comma (bool canAssign) {
    // parser.previous points to the comma
    // expression();
}

static void printStatement () {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value");
    emitByte(OP_PRINT);
}

static void expressionStatement () {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression");
    emitByte(OP_POP); // to return the stack to its original state 
}

static uint8_t parseVariable (const char* msg) {
    consume(TOKEN_IDENTIFIER, msg);
    return identifierConstant(&parser.previous);
}

static void variable (bool canAssign) {
    namedVariable(parser.previous, canAssign);
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

    [TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},

    // comparisons
    [TOKEN_BANG_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_EQUAL_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_LESS] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GREATER] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},
    
    // literals
    [TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE},
    [TOKEN_STRING] = {string, NULL, PREC_NONE},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE},

    // booleans
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE},
    [TOKEN_TRUE] = {literal, NULL, PREC_NONE},
    [TOKEN_NIL] = {literal, NULL, PREC_NONE},
    
    // binary operands
    [TOKEN_BANG] = {unary, NULL, PREC_NONE},
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

static void statement () {
    if (match(TOKEN_PRINT)) {
        printStatement ();
    } else {
        expressionStatement ();
    }
}

// declarations
static void varDeclaration () {
    // parser.current points to the var
    advance(); // points to the identifier

    // consume the identifier
    uint8_t global = parseVariable("Expect variable name");

    if (match(TOKEN_EQUAL)) {
        expression();
    } else {
        emitByte(OP_NIL);
    }
    
    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration");

    defineVariable(global);
}

static void funDeclaration () {

}

static void declaration () {

    switch (peek()) {
        case TOKEN_VAR: varDeclaration (); break;
        case TOKEN_FUN: funDeclaration (); break;

        default:
            statement();
            break;
    }
    
    // synchronize
    if (parser.panicMode) synchonize ();
}

static void parsePrecedence (Precedence precedence) {
    advance ();

    ParseFn prefixRule = getRule(parser.previous.ttype) -> prefix;

    if (prefixRule == NULL) {
        errorAtCurrent("Expect expression");
        return;
    }

    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);

    while (precedence <= getRule(parser.current.ttype) -> precedence) {
        advance ();
        ParseFn infixRule = getRule(parser.previous.ttype) -> infix;

        if (infixRule == NULL) {
            errorAtCurrent("Not good");
            return;
        }

        infixRule(canAssign);
    }

    if (canAssign && match(TOKEN_EQUAL)) {
        errorAtCurrent("Invalid assignment target");
    }
}

bool compile (const char* source, Chunk* chunk) {
    initScanner(source);
    compilingChunk = chunk;

    parser.hadError = false;
    parser.panicMode = false;

    advance();   

    // program -> seqeunce of statements 
    while (!match(TOKEN_EOF)) {
        declaration();
    }

    consume (TOKEN_EOF, "Expect end of expression");

    endCompiler();

    return !parser.hadError;
}
