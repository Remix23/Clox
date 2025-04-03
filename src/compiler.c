#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "compiler.h"
#include "common.h"
#include "chunk.h"
#include "scanner.h"
#include "object.h"
#include "hashmap.h"
#include "memory.h"

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
    PREC_COMMA,      // ,
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

typedef struct {
    int depth;
    bool isCaptured;
    Token name;
} Local;

typedef struct {
    uint8_t index;
    bool isLocal;
} Upvalue;

typedef enum {
    TYPE_FUNCTION,
    TYPE_SCRIPT,
    TYPE_METHOD,
} FunctionType;
 
typedef struct Compiler {
    struct Compiler* enclosing;

    ObjFunction* function;
    FunctionType ftype;

    Local locals[UINT8_MAX + 1];
    int localCount;
    int scopeDepth;

    Upvalue upValues [UINT8_COUNT];
} Compiler;

typedef struct ClassCompiler {
    struct ClassCompiler* enclosing;
} ClassCompiler;

Parser parser;
Compiler* current = NULL;
ClassCompiler* currentClass = NULL;
Chunk* compilingChunk;

// ========= Parsing Declarations =========

static void expression ();
static void statement ();
static void declaration ();
static void varDeclaration ();
static uint8_t parseVariable (const char* msg);
static void function (FunctionType ftype);
static void expressionStatement ();
static void beginScope ();
static void endScope ();
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
    return &current -> function -> chunk;
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

static int emitJump (uint8_t instruction) {
    emitByte(instruction);

    emitByte(0xff);
    emitByte(0xff);

    // emit empty byte the jump offset

    // return the offset of the jump in the chunk code
    return currentChunk() -> count - 2;
}

static void emitJumpBack (int start) {
    emitByte(OP_JUMP_BACK);

    int curr_pos = currentChunk() -> count;

    // printf("Current position: %d\n", curr_pos);

    // account for the two bytes the pointer moves while reading the command
    int jump = curr_pos + 2 - start;

    if (jump > UINT16_MAX) {
        errorAtCurrent("Loop body too large");
    }

    uint8_t b1 = (jump >> 8) & 0xff;
    uint8_t b2 = jump & 0xff;

    emitBytes(b1, b2);
} 

static void patchJump (int offset) {
    // size of the chunk after parsing statements
    int top = currentChunk() -> count;

    // -2 offsets for the bytes used for the jump instruction
    int jump = top - offset - 2;

    // fix the value
    if (jump > UINT8_MAX) {
        errorAtCurrent("Too much code to jump over");
    }

    currentChunk() -> code[offset] = (jump >> 8) & 0xff;
    currentChunk() -> code[offset + 1] = jump & 0xff;
}

static void emitReturn () {
    emitByte(OP_NIL);
    emitByte(OP_RETURN);
}
static ObjFunction* endCompiler () {
    emitReturn();

    ObjFunction* func = current -> function;

    #ifdef DEBUG_PRINT_CODE 
    if (!parser.hadError) {
        disassembleChunk(currentChunk(), func -> name != NULL ? func -> name -> chars : "<script>");
    }
#endif
    
    current = current -> enclosing;
    return func;
}

static void initCompiler (Compiler* compiler, FunctionType ftype) {
    compiler -> enclosing = current;
    compiler -> function = NULL;
    compiler -> ftype = ftype;
    
    compiler -> localCount = 0;
    compiler -> scopeDepth = 0;

    compiler -> function = newFunction();
    current = compiler;

    if (ftype != TYPE_SCRIPT) {
        current -> function -> name = copyString(parser.previous.start, parser.previous.length);
    }

    Local* local = &current -> locals[current -> localCount++];
    local->isCaptured = false;

    if (ftype != TYPE_FUNCTION) {
        local -> name.start = "this"; // reserved in the VM stack for the first call frame (aka the main function)
        local -> name.length = 4;
    } else {
        local->name.start = "";
        local->name.length = 0;
    }
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

static bool identifiersEqual (Token* a, Token* b) {
    if (a -> length != b -> length) return false;
    return memcmp(a -> start, b -> start, a -> length) == 0;
}

static void markInitialized () {
    if (current -> scopeDepth == 0) return;
    current -> locals[current -> localCount - 1].depth = current -> scopeDepth;
}

static void addLocal (Token name) {
    if (current -> localCount == UINT8_COUNT) {
        errorAtCurrent("Too many local variables in function");
        return;
    }

    Local* local = &current -> locals[current -> localCount++];
    local -> name = name;
    local -> depth = -1; // not initialized
    local ->isCaptured = false;
}

static int addUpvalue (Compiler* compiler, uint8_t index, bool isLocal) {
    int upValueCount = compiler -> function -> upValuesCount;
    for (int i = 0; i < upValueCount; i++) {
        Upvalue* upvalue = &compiler -> upValues[i];
        if (upvalue -> index == index && upvalue -> isLocal == isLocal) {
            return i;
        }
    }

    if (upValueCount == UINT8_MAX) {
        errorAtCurrent("Too many closure variables in function/=.");
        return 0;
    }

    compiler -> upValues[upValueCount].isLocal = isLocal;
    compiler -> upValues[upValueCount].index = index;


    return compiler -> function -> upValuesCount++;
}

static int resolveLocal (Compiler* compiler, Token* name) {
    for (int i = compiler -> localCount - 1; i >= 0; i--) {
        Local* local = &compiler -> locals[i];

        if (identifiersEqual(name, &local -> name)) {
            if (local -> depth == -1) {
                errorAtCurrent("Cannot read local variable in its own initializer");
            }
            return i;
        }
    }
    return -1;
}

static int resolveUpvalue (Compiler* compiler, Token* name) {
    if (compiler -> enclosing == NULL) return -1; // global scopre

    int local = resolveLocal(compiler -> enclosing, name);
    if (local != -1) {
        compiler ->enclosing->locals[local].isCaptured = true;
        return addUpvalue (compiler, (uint8_t) local, true);
    }

    int upValue = resolveUpvalue(compiler -> enclosing, name);
    if (upValue != -1) 
        return addUpvalue(compiler, (uint8_t) upValue, false);

    return -1;
}

static void declareVariable () {
    if (current -> scopeDepth == 0) return;

    Token* name = &parser.previous; // identifier

    // check if it exists in the current scope
    for (int i = current -> localCount - 1; i >= 0; i--) {
        Local* local = &current -> locals[i];
        if (local -> depth != -1 && local -> depth < current -> scopeDepth) {
            break;
        }

        if (identifiersEqual(name, &local -> name)) {
            errorAtCurrent("Variable with this name already declared in this scope");
        }
    }
    
    addLocal(*name);
}

static void defineVariable (uint8_t global) {

    if (current -> scopeDepth > 0) {
        markInitialized();
        return;
    }

    emitBytes(OP_DEFINE_GLOBAL, global); // global is the index in the constant table
}

static void namedVariable (Token name, bool canAssign) {
    u_int8_t getOp, setOp;

    int arg = resolveLocal(current, &name);
    if (arg != -1) {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    } else if ((arg = resolveUpvalue(current, &name)) != -1 ) {
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
    } else {
        arg = identifierConstant(&name); // store the identifier as a string in te hash table
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }

    if (match(TOKEN_EQUAL) && canAssign) {
        parsePrecedence(PREC_ASSIGNMENT);
        emitBytes(setOp, (uint8_t)arg);
    } else {
        emitBytes(getOp, (uint8_t)arg);
    }
}

static uint8_t parseArguments () {
    // '(' is already consumed
    uint8_t argCount = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do
        {
            parsePrecedence(PREC_ASSIGNMENT);
            argCount++;
            if (argCount == 255) {
                errorAtCurrent("Can't have more than 255 arguments.");
            }
        } while (match(TOKEN_COMMA));
    }

    consume(TOKEN_RIGHT_PAREN, "Expect ')' after paremeter list.");
    return argCount;
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

static void and_ (bool canAssign) {

    // exp1 && exp2 -> if exp1 is false jump after expr2

    int and_off = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP); // pop result of epxr1 
    parsePrecedence(PREC_AND); // expr2
    
    // expr2 left on stack (since expr1 is true the result of and it equal to expr2)

    patchJump(and_off);
}

static void or_ (bool canAssign) {
    int whole = emitJump(OP_JUMP_IF_FALSE);
    int expr1_true = emitJump(OP_JUMP);

    patchJump(whole);
    emitByte(OP_POP);

    parsePrecedence(PREC_OR);

    patchJump(expr1_true);

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

static void call (bool canAssign) {
    // '(' is consumed
    uint8_t n_args = parseArguments();
    emitBytes(OP_CALL, n_args);
}

static void dot (bool canAssign) {
    consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
    uint8_t name = identifierConstant(&parser.previous);

    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitBytes(OP_SET_PROPERTY, name);
    } else {
        emitBytes(OP_GET_PROPERTY, name);
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

    int else_branch = emitJump(OP_JUMP_IF_FALSE);

    // parsing the true expression
    emitByte(OP_POP);
    parsePrecedence(PREC_ASSIGNMENT);

    int exit = emitJump(OP_JUMP);
    patchJump(else_branch);

    emitByte(OP_POP);

    // consume the colon
    consume(TOKEN_COLON, "Expect ':' after then branch");

    parsePrecedence(PREC_ASSIGNMENT);
    patchJump(exit);
}

static void comma (bool canAssign) {
    // parser.previous points to the comma
    emitByte(OP_POP);
    parsePrecedence(PREC_ASSIGNMENT);
}

static void printStatement () {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value");
    emitByte(OP_PRINT);
}

static void beginScope () {
    current -> scopeDepth++;
}

static void endScope () {

    current -> scopeDepth--;

    while (current -> localCount > 0 && current -> locals[current -> localCount - 1].depth > current -> scopeDepth) {
        if (current -> locals[current -> localCount - 1].isCaptured) {
            emitByte(OP_CLOSE_CAPTURE);
        } else {
            emitByte(OP_POP);
        }
        current -> localCount--;
    }
}

static void blockStatement () {
    // parsing while not eof or the closing brace
    while (!check(TOKEN_EOF) && !check(TOKEN_RIGHT_BRACE)) {
        declaration();
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block");
}

static void ifStatement () {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if' statement");

    expression(); // leave the result on the stack

    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition");

    // emit jump
    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP); // pop the condition, a statement always returns the stack to its original state
    statement();
    int elseJump = emitJump(OP_JUMP);

    // patch jump
    patchJump(thenJump);

    emitByte(OP_POP); // one of the pops will always execute the other skipped

    if (match(TOKEN_ELSE)) statement();

    patchJump(elseJump);
}

static void whileStatement () {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while' statement");

    int loop_start = currentChunk() -> count;

    // printf("Loop start: %d\n", loop_start);

    expression();

    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition");

    int exit_jump = emitJump(OP_JUMP_IF_FALSE);

    emitByte(OP_POP); // pop the condition

    statement();

    emitJumpBack(loop_start);

    patchJump(exit_jump);

    emitByte(OP_POP); // pop the condition when exiting the loop
}

static void forStatement () {

    beginScope();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for' statement");

    // initializer
    // can be a declaration or an expression
    if (match(TOKEN_VAR)) {
        varDeclaration();
    } else if (match(TOKEN_SEMICOLON)) {
        // no initializer
    } else {
        expressionStatement();
    }

    int start_loop = currentChunk() -> count;

    // condition
    int exit_jump = -1;

    if (match(TOKEN_SEMICOLON)) {
        // no condition
    } else {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after loop condition");
        exit_jump = emitJump(OP_JUMP_IF_FALSE);
    }

    emitByte(OP_POP); // pop th condition assuming it is true

    int body_jump = emitJump(OP_JUMP);

    int increment_start = currentChunk() -> count;

    // check for increment
    if (match(TOKEN_RIGHT_PAREN)) {
        // no increment
    } else {
        expression ();
        emitByte(OP_POP);
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after the loop initializer");
    }

    emitJumpBack(start_loop);

    patchJump(body_jump);

    // parse loop body
    statement();

    emitJumpBack(increment_start);

    if (exit_jump != -1) {
        patchJump(exit_jump);
        emitByte(OP_POP);
    }

    endScope();
}

static void breakStatement () {

}

static void continueStatement () {

}

static void returnStatement () {

    if (current -> ftype == TYPE_SCRIPT) {
        errorAtCurrent("Can't have return outside of function body.");
    }
    if (match(TOKEN_SEMICOLON)) {
        emitReturn();
    } else {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after the return value.");
        emitByte(OP_RETURN);
    }
}

static void function (FunctionType ftype) {
    Compiler compiler;
    initCompiler(&compiler, ftype);
    beginScope();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");

    if (!check(TOKEN_RIGHT_PAREN)) {
        do
        {
            current -> function->arity++;
            if (current -> function->arity > 255) {
                errorAtCurrent("Can't have more than 255 paremeters.");
            }
            uint8_t constant = parseVariable("Expect parameter name.");
            defineVariable(constant);
        } while (match(TOKEN_COMMA));   
    }

    consume(TOKEN_RIGHT_PAREN, "Expect ')' after function name.");

    consume(TOKEN_LEFT_BRACE, "Expect '{' after parameter list.");

    blockStatement(); // consumes the trailing bracket

    ObjFunction* function = endCompiler();
    emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));

    // closure support
    for (int i = 0; i < function ->upValuesCount; i++ ) {
        emitByte(compiler.upValues[i].isLocal ? 1 : 0);
        emitByte(compiler.upValues[i].index);
    }
}

static void method () {
    consume(TOKEN_IDENTIFIER, "Expect method name.");
    uint8_t index = identifierConstant(&parser.previous);

    FunctionType ftype = TYPE_METHOD;
    function(ftype);

    emitBytes(OP_METHOD, index);
}

static void expressionStatement () {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression");
    emitByte(OP_POP); // to return the stack to its original state 
}

static uint8_t parseVariable (const char* msg) {
    consume(TOKEN_IDENTIFIER, msg);

    declareVariable ();
    if (current -> scopeDepth > 0) return 0;

    return identifierConstant(&parser.previous);
}

static void variable (bool canAssign) {
    namedVariable(parser.previous, canAssign);
}

static void this_ (bool canAssign) {

    if (currentClass == NULL) {
        errorAtCurrent("Can't use 'this' outside of a class");
        return;
    }

    variable(false);
}

// ========= Parsing rules =========

ParserRule rules [] = {
    [TOKEN_LEFT_PAREN] = {grouping, call, PREC_CALL},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},

    // colons and dots
    [TOKEN_COMMA] = {NULL, comma, PREC_COMMA},
    [TOKEN_DOT] = {NULL, dot, PREC_CALL},
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
    [TOKEN_AND] = {NULL, and_, PREC_AND},
    [TOKEN_OR] = {NULL, or_, PREC_OR},

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
    [TOKEN_THIS] = {this_, NULL, PREC_NONE},

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
    parsePrecedence(PREC_COMMA);
}

static void statement () {
    if (match(TOKEN_PRINT)) {
        printStatement ();
    } else if (match(TOKEN_LEFT_BRACE)) {
        beginScope();
        blockStatement ();
        endScope();
    } else if (match(TOKEN_IF)) {
        ifStatement();
    } else if (match(TOKEN_WHILE)) {
        whileStatement();
    } else if (match(TOKEN_FOR)) {
        forStatement();
    } else if (match(TOKEN_CONTINUE)) {
        continueStatement();
    } else if (match(TOKEN_BREAK)) {
        breakStatement();
    } else if (match(TOKEN_RETURN)) {
        returnStatement();
    } else {
        expressionStatement ();
    }
}

// declarations
static void varDeclaration () {
    // parser.current points to the identifier

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
    uint8_t global = parseVariable("Expect function name");
    markInitialized();

    function(TYPE_FUNCTION);
    defineVariable(global);
}

static void classDeclaration () {

    // push the new class on the compiler

    // current at the identifier
    consume(TOKEN_IDENTIFIER, "Expect a class name.");
    Token className = parser.previous;
    uint8_t nameConstant = identifierConstant(&parser.previous);
    declareVariable();

    emitBytes(OP_CLASS, nameConstant);
    defineVariable(nameConstant);

    ClassCompiler classCompiler;
    classCompiler.enclosing = currentClass;
    currentClass = &classCompiler;

    consume(TOKEN_LEFT_BRACE, "Expect '{' after the class name.");

    namedVariable(className, false);
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF))
    {
        method();
    }
    
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after a the class declaration.");
    emitByte(OP_POP);

    currentClass = currentClass->enclosing;
}

static void declaration () {

    switch (peek()) {
        case TOKEN_VAR: advance(); varDeclaration (); break;
        case TOKEN_FUN: advance(); funDeclaration (); break;
        case TOKEN_CLASS: advance(); classDeclaration (); break;

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

ObjFunction* compile (const char* source) {
    initScanner(source);
    Compiler compiler;
    initCompiler(&compiler, TYPE_SCRIPT);
    compilingChunk = currentChunk();

    parser.hadError = false;
    parser.panicMode = false;

    advance();   

    // program -> seqeunce of statements 
    while (!match(TOKEN_EOF)) {
        declaration();
    }

    ObjFunction* func = endCompiler();
    return parser.hadError ? NULL : func;
}

void markCompilerRoots () {
    Compiler* compiler = current;
    while (compiler != NULL) {
        markObject((Obj*) compiler -> function);
        compiler = compiler -> enclosing;
    }
}