#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "vm.h"
#include "memory.h"
#include "common.h"
#include "debug.h"
#include "compiler.h"
#include "value.h"
#include "object.h"

VM vm;

static void resetStack () {
    vm.stackTop = vm.stack;
}

static void runtimeError (const char* format, ...) {
    // ! way of getting variable number of arguments to function
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);

    fputs("\n", stderr);

    size_t instruction = vm.ip - vm.chunk -> code - 1;
    int line = vm.chunk -> lines[instruction];

    fprintf(stderr, "[line %d] in script\n", line);
    resetStack();
}

void initVM () {
    vm.objects = NULL;
    resetStack();
}

static Value peek (int distance) {
    return vm.stackTop[-1 - distance];
}

static bool isFalsey (Value value) {
    // zero is falsey
    if (IS_NUMBER(value)) return AS_NUMBER(value) == 0;
    if (IS_NIL(value)) return true;

    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

void concatenate () {
    ObjString* b = AS_STRING(pop());
    ObjString* a = AS_STRING(pop());

    int out_length = a ->length + b -> length;

    char* out = ALLOCATE(char, out_length + 1);
    memcpy(out, a -> chars, a -> length);
    memcpy(out + a -> length, b -> chars, b -> length);
    out[out_length] = '\0';

    ObjString* out_string = takeString(out, out_length);
    push(OBJ_VAL(out_string));
}

void freeVM () {
    freeObjects();
}

void push (Value value) {
    *vm.stackTop = value;
    vm.stackTop++;
}

Value pop () {
    vm.stackTop--;
    return *vm.stackTop;
}

static InterpretResult run () {
    #define READ_BYTE() (*vm.ip++) 
    #define READ_CONSTANT() (vm.chunk -> constants.values[READ_BYTE()])
    #define BINARY_OP(valueType, op) \
        do { \
            if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
                runtimeError("Operands must be numbers."); \
                return INTERPRET_RUNTIME_ERROR; \
            } \
            double b = AS_NUMBER(pop()); \
            double a = AS_NUMBER(pop()); \
            push(valueType(a op b)); \
        } while (false)
        /* *(vm.stackTop - 1) = *(vm.stackTop - 1) op b; \ */
    for (;;) {

#ifdef DEBUG_TRACE_EXECUTION
        printf("          ");
        for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
            printf("[ ");
            printValue(*slot);
            printf(" ]");
        }
        printf("\n");
        disassembleInstruction(vm.chunk, \
            (int)(vm.ip - vm.chunk -> code));
#endif     

        uint8_t instruction;
        
        switch (instruction = READ_BYTE())
        {
            case OP_RETURN: {
                printValue(pop());
                printf("\n");
                return INTERPRET_OK;
            }
            case OP_CONSTANT:
            {
                Value constant = READ_CONSTANT();
                push(constant);
                break;
            }
            case OP_CONSTANT_LONG:
            {
                break;
            }

            case OP_ADD: {
                if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
                    concatenate();
                } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(NUMBER_VAL(a + b));
                } else {
                    runtimeError("Operands must be two numbers or two strings.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
            case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
            case OP_DIVIDE: BINARY_OP(NUMBER_VAL, /); break;

            case OP_NEGATE:
            { // switched to in place negation
                if (!IS_NUMBER(peek(0))) {
                    runtimeError ("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(NUMBER_VAL(-AS_NUMBER(pop())));
                // *(vm.stackTop - 1) = - *(vm.stackTop - 1);
                break;
            }

            case OP_FALSE: push(BOOL_VAL(false)); break;
            case OP_TRUE: push(BOOL_VAL(true)); break;
            case OP_NIL: push(NIL_VAL); break;

            case OP_NOT: {
                push(BOOL_VAL(isFalsey(pop())));
                break;
            }

            case OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(valuesEqual(a, b)));
                break;
            }

            case OP_LESS: BINARY_OP(BOOL_VAL, <); break;
            case OP_GREATER: BINARY_OP(BOOL_VAL, >); break;

            default:
            break;
        }
    }
    return INTERPRET_OK;
    
#undef READ_BYYE
#undef READ_CONSTANT
}
    
InterpretResult interpret (const char* source) {
    Chunk chunk;

    initChunk(&chunk);

    if (!compile(source, &chunk)) {
        freeChunk(&chunk);
        return INTERPRET_COMPILE_ERROR;
    }

    vm.chunk = &chunk;
    vm.ip = vm.chunk -> code;

    InterpretResult res = run();

    freeChunk(&chunk);
    return res;
}