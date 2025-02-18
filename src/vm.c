#include <stdio.h>

#include "vm.h"
#include "common.h"

VM vm;


void initVM () {}

void freeVM () {}


static InterpretResult run () {
    #define READ_BYTE() (*vm.ip++) 
    #define READ_CONSTANT() (vm.chunk -> constants.values[READ_BYTE()])
    
    for (;;) {
        uint8_t instruction;
        
        switch (instruction = READ_BYTE())
        {
            case OP_RETURN:
            return INTERPRET_OK;
            
            case OP_CONSTANT:
            {
                Value constant = READ_CONSTANT();
                printValue(constant);
                printf("\n");
                break;
            }
            case OP_CONSTANT_LONG:
            {
                
            }
            
            default:
            break;
        }
    }
    return INTERPRET_OK;
    
#undef READ_BYYE
#undef READ_CONSTANT
}
    
InterpretResult interpret (Chunk* chunk) {
    vm.chunk = chunk;
    vm.ip = vm.chunk -> code;
    return run();
}