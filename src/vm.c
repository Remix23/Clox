#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vm.h"
#include "memory.h"
#include "common.h"
#include "debug.h"
#include "compiler.h"
#include "value.h"
#include "object.h"

VM vm;

static Value clockNative (int argCount, Value* args) {
    return NUMBER_VAL((double) clock() / CLOCKS_PER_SEC);
}

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

    for (int i = vm.frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm.frames[i];
        ObjFunction* func = frame -> function;
        size_t instruction = frame -> ip - func->chunk.code - 1;
        fprintf(stderr, "[line %d] in ", func -> chunk.lines[instruction]);
        if (func->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", func -> name ->chars);
        }
    }
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


void push (Value value) {
    *vm.stackTop = value;
    vm.stackTop++;
}

Value pop () {
    vm.stackTop--;
    return *vm.stackTop;
}

static bool call(ObjFunction* func, int argCount) {
    if (argCount != func->arity) {
        runtimeError("Expected %d arguments but got %d", func->arity, argCount);
        return false;
    }

    if (vm.frameCount == MAX_FRAMES) {
        runtimeError("Stack overflow");
        return false;
    }

    CallFrame* frame = &vm.frames[vm.frameCount++]; // initialize new call frame
    frame -> function = func;
    frame -> ip = func -> chunk.code;
    frame -> slots = vm.stackTop - argCount - 1;
    return true;
}

static bool callValue (Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee))
        {
        case OBJ_FUNCTION:
                return call(AS_FUNCTION(callee), argCount);
        case OBJ_NATIVE: {
            NativeFn native = AS_NATIVE(callee);
            Value res = native(argCount, vm.stackTop - argCount);
            vm.stackTop -= argCount + 1;
            push(res);
            return true;
        }
        default:
            break; // non callable stuff
        }
    }
    runtimeError("Can only call functions and classes");
    return false;
}

static void defineNative (const char* name, NativeFn func) {
    push(OBJ_VAL(copyString(name, (int)strlen(name))));
    push(OBJ_VAL(newNative(func)));
    hashMapSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
    pop();
    pop();
}

static InterpretResult run () {

    CallFrame* frame = &vm.frames[vm.frameCount - 1];

    #define READ_BYTE() (*(frame -> ip++)) 
    #define READ_CONSTANT() (frame -> function -> chunk.constants.values[READ_BYTE()])
    #define READ_SHORT() \
        (frame -> ip += 2, \
        (uint16_t)((frame -> ip[-2] << 8) | frame -> ip[-1]))
    #define READ_STRING() AS_STRING(READ_CONSTANT())
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
        disassembleInstruction(&frame -> function -> chunk, \
            (int)(frame -> ip - frame -> function -> chunk.code));
#endif     

        uint8_t instruction;
        
        switch (instruction = READ_BYTE())
        {
            case OP_RETURN: {
                // exit -> to change later with functions and multiple chunks
                vm.frameCount--;
                Value res = pop();
                if (vm.frameCount == 0) {
                    pop();
                    return INTERPRET_OK;
                }
                
                vm.stackTop = frame->slots;
                push(res);
                frame = &vm.frames[vm.frameCount - 1];
                break;
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

            case OP_PRINT: {
                printValue(pop());
                printf("\n");
                break;
            }

            case OP_POP: pop(); break;

            case OP_DEFINE_GLOBAL: {
                ObjString* name = READ_STRING();
                hashMapSet(&vm.globals, name, peek(0));
                pop();
                break;
            }

            case OP_GET_GLOBAL: {
                ObjString* name = READ_STRING();
                Value value;
                if (!hashMapGet(&vm.globals, name, &value)) {
                    runtimeError("Undefined variable '%s'.", name -> chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(value);
                break;
            }

            case OP_SET_GLOBAL: {
                ObjString* name = READ_STRING();

                // a new key 
                if (hashMapSet(&vm.globals, name, peek(0))) {
                    hashMapDelete(&vm.globals, name);
                    runtimeError("Undefined variable '%s'.", name -> chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }

            case OP_GET_LOCAL: {
                uint8_t index = READ_BYTE();
                push(frame -> slots[index]);
                break;
            }

            case OP_SET_LOCAL: {
                uint8_t index = READ_BYTE();
                frame -> slots[index] = peek(0);
                break;
            }

            case OP_JUMP_IF_FALSE: {
                uint16_t offset = (uint16_t)READ_BYTE() << 8;
                offset = offset | READ_BYTE();
                if (isFalsey(peek(0))) frame -> ip += offset;
                break;
            }

            case OP_JUMP: 
            {
                uint16_t offset = (uint16_t)READ_BYTE() << 8;
                offset = offset | READ_BYTE();
                frame -> ip += offset;
                break;
            }

            case OP_JUMP_BACK: {
                uint16_t offset = (uint16_t)READ_BYTE() << 8;
                offset = offset | READ_BYTE();
                frame -> ip -= offset;
                break;
            }
            case OP_CALL: {
                int argCount = READ_BYTE();
                
                if (!callValue(peek(argCount), argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }

            default:
            break;
        }
    }
    return INTERPRET_OK;
    
#undef READ_BYYE
#undef READ_SHORT
#undef READ_STRING
#undef READ_CONSTANT
}

void initVM () {
    vm.objects = NULL;
    vm.frameCount = 0;
    initHashMap(&vm.strings, 10);
    initHashMap(&vm.globals, 5); 
    resetStack();

    defineNative("clock", clockNative);
}
    
InterpretResult interpret (const char* source) {
    ObjFunction* func = compile(source);

    if (func == NULL) return INTERPRET_COMPILE_ERROR;

    push(OBJ_VAL(func));

    call(func, 0);

    return run ();
}


void freeVM () {
    freeObjects();
    freeHashMap(&vm.strings);
    freeHashMap(&vm.globals);
}