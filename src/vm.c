#include <stdio.h>

#include "vm.h"
#include "common.h"
#include "debug.h"
#include "compiler.h"

VM vm;

static void resetStack () {
    vm.stackTop = vm.stack;
}

void initVM () {
    resetStack();
}

void freeVM () {}

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
    #define BINARY_OP(op) \
        do { \
            double b = pop(); \
            *(vm.stackTop - 1) = *(vm.stackTop - 1) op b; \
        } while (false)

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


            case OP_ADD: BINARY_OP(+); break;
            case OP_SUBTRACT: BINARY_OP(-); break;
            case OP_MULTIPLY: BINARY_OP(*); break;
            case OP_DIVIDE: BINARY_OP(/); break;

            case OP_NEGATE:
            { // switched to in place negation
                *(vm.stackTop - 1) = - *(vm.stackTop - 1);
                break;
            }

            default:
            break;
        }
    }
    return INTERPRET_OK;
    
#undef READ_BYYE
#undef READ_CONSTANT
}
    
InterpretResult interpret (const char* source) {
    compile(source);
    return INTERPRET_OK;
}