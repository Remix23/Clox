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
    vm.frameCount = 0;
    vm.openUpvalues = NULL;
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
        ObjFunction* func = frame ->closure->rawFunc;
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
    ObjString* b = AS_STRING(peek(0));
    ObjString* a = AS_STRING(peek(1));

    int out_length = a ->length + b -> length;

    char* out = ALLOCATE(char, out_length + 1);
    memcpy(out, a -> chars, a -> length);
    memcpy(out + a -> length, b -> chars, b -> length);
    out[out_length] = '\0';

    ObjString* out_string = takeString(out, out_length);

    pop();
    pop();
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

static bool call(ObjClosure* closure, int argCount) {
    if (argCount != closure -> rawFunc ->arity) {
        runtimeError("Expected %d arguments but got %d", closure->rawFunc->arity, argCount);
        return false;
    }

    if (vm.frameCount == MAX_FRAMES) {
        runtimeError("Stack overflow");
        return false;
    }

    CallFrame* frame = &vm.frames[vm.frameCount++]; // initialize new call frame
    frame -> closure = closure;
    frame -> ip = closure->rawFunc -> chunk.code;
    frame -> slots = vm.stackTop - argCount - 1;
    return true;
}

static bool callValue (Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee))
        {
        case OBJ_BOUND_METHOD: {
            ObjBoundMethod* bound = AS_BOUNDMETHOD(callee);
            vm.stackTop[-argCount - 1] = bound->receiver;
            return call(bound->method, argCount);
        }
        case OBJ_CLOSURE:
                return call(AS_CLOSURE(callee), argCount);
        case OBJ_NATIVE: {
            ObjNativeFn* native = AS_NATIVE(callee);
            if (argCount != native -> arity) {
                runtimeError("Expected %d arguments but got %d", native -> arity, argCount);
                return false;
            }

            Value res = native ->func(argCount, vm.stackTop - argCount);
            vm.stackTop -= argCount + 1;
            push(res);
            return true;
        }
        case OBJ_CLASS: {
            ObjClass* clas = AS_CLASS(callee);
            vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(clas));
            return true;
        }
        default:
            break; // non callable stuff
        }
    }
    runtimeError("Can only call functions and classes");
    return false;
}

static void defineNative (const char* name, NativeFn func, int arity) {
    push(OBJ_VAL(copyString(name, (int)strlen(name))));
    push(OBJ_VAL(newNative(arity, func)));
    hashMapSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
    pop();
    pop();
}

static ObjUpvalue* captureUpvalue (Value* local) {
    ObjUpvalue* prevUpvalue = NULL;
    ObjUpvalue* upvalue = vm.openUpvalues;
    while (upvalue != NULL && upvalue -> location > local) {
        prevUpvalue = upvalue;
        upvalue = prevUpvalue->next;
    }

    if (upvalue!= NULL && upvalue->location == local) {
        return upvalue;
    }

    ObjUpvalue* createdUpvalue = newUpvalue(local);
    createdUpvalue->next = upvalue;

    if (prevUpvalue == NULL) {
        vm.openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }
    return createdUpvalue;
}

static void closeUpvalues (Value* last) {
    while (vm.openUpvalues != NULL && 
        vm.openUpvalues -> location >= last) {
            ObjUpvalue* upvalue = vm.openUpvalues;
            upvalue -> closed = *upvalue->location;
            upvalue->location = &upvalue -> closed;
            vm.openUpvalues = upvalue->next;
    }
}

static void defineMethod (ObjString* name) {
    Value method = peek(0);
    ObjClass* clas = AS_CLASS(peek(1));
    hashMapSet(&clas->methods, name, method);
    pop();
}

static bool bindMethod (ObjClass* clas, ObjString* name) {
    Value method;
    if (!hashMapGet(&clas->methods, name, &method)) {
        runtimeError("Undefined property %s", name->chars);
        return false;
    }

    ObjBoundMethod* bound = newBoundMethod(peek(0), AS_CLOSURE(method));

    pop();
    push(OBJ_VAL(bound));
    return true;
}

static InterpretResult run () {

    CallFrame* frame = &vm.frames[vm.frameCount - 1];

    #define READ_BYTE() (*(frame -> ip++)) 
    #define READ_CONSTANT() (frame->closure->rawFunc -> chunk.constants.values[READ_BYTE()])
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
        disassembleInstruction(&frame->closure->rawFunc -> chunk, \
            (int)(frame -> ip - frame->closure->rawFunc -> chunk.code));
#endif     

        uint8_t instruction;
        
        switch (instruction = READ_BYTE())
        {
            case OP_RETURN: {

                // exit -> to change later with functions and multiple chunks
                Value res = pop();
                closeUpvalues(frame->slots);
                vm.frameCount--;
                
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

            case OP_CLOSURE: {
                ObjFunction* func = AS_FUNCTION(READ_CONSTANT());
                ObjClosure* closure = newClosure(func);
                push(OBJ_VAL(closure));

                for (int i = 0; i < closure ->upvalueCount; i++) {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal) {
                        closure -> upvalues[i] = captureUpvalue(frame -> slots + index);
                    } else {
                        closure -> upvalues[i] = frame->closure->upvalues[index];
                    }
                }
                
                break;
            }

            case OP_CLOSE_CAPTURE: {
                closeUpvalues(vm.stackTop - 1);
                pop();
                break;
            }

            case OP_GET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                push(*frame->closure->upvalues[slot]->location);
                break;
            }

            case OP_SET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                *frame ->closure->upvalues[slot]->location = peek(0);
                break;
            }

            case OP_CLASS: {
                ObjString* name = READ_STRING();
                push(OBJ_VAL(newCLass(name)));
                break;
            }
            case OP_GET_PROPERTY: {

                if (!IS_ISTANCE(peek(0))) {
                    runtimeError("Only instances have properties.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjInstance* instance = AS_INSTANCE(peek(0));
                ObjString* name = READ_STRING();

                Value value;
                if (hashMapGet(&instance->fields, name, &value)) {
                    pop();
                    push(value);
                    break;
                }

                if (!bindMethod(instance->clas, name)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SET_PROPERTY: {
                if (!IS_ISTANCE(peek(1))) {
                    runtimeError("Only instance have field.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjInstance* instance = AS_INSTANCE(peek(1));
                ObjString* name = READ_STRING();
                hashMapSet(&instance->fields, name, peek(0));
                Value val = pop();
                pop();
                push(val);
                break;
            }
            case OP_METHOD:
                defineMethod (READ_STRING());
                break;

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

    vm.grayCapacity = 0;
    vm.grayCount = 0;
    vm.grayStack = NULL;

    vm.bytesAlocated = 0;
    vm.nextGC = 1024 * 1024;

    initHashMap(&vm.strings, 10);
    initHashMap(&vm.globals, 5); 
    resetStack();

    defineNative("clock", clockNative, 0);
}
    
InterpretResult interpret (const char* source) {
    ObjFunction* func = compile(source);

    if (func == NULL) return INTERPRET_COMPILE_ERROR;

    push(OBJ_VAL(func));
    ObjClosure* closure = newClosure(func);
    pop();
    push(OBJ_VAL(closure));
    call(closure, 0);

    return run ();
}


void freeVM () {
    freeObjects();
    freeHashMap(&vm.strings);
    freeHashMap(&vm.globals);

    free(vm.grayStack);
}