#include <stdio.h>
#include <string.h>

#include "object.h"
#include "memory.h"
#include "value.h"
#include "vm.h"
#include "hashmap.h"

static uint32_t hashString(const char* key, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= key[i];
        hash *= 16777619;
    }
    return hash;
}

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

static Obj* allocateObject(size_t size, ObjType otype) {
    Obj* object = (Obj*)reallocate(NULL, 0, size);
    object->otype = otype; 
    object->isMarked = false;

    object->next = vm.objects;
    vm.objects = object;

#ifdef DEBUG_LOG_GC
    printf("%p allocate %zu for %d\n", (void*) object, size, otype);
#endif

    return object;
}

static ObjString* allocateString(char* chars, int length, uint32_t hash) {
    ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);

    string-> length = length;
    string -> chars = chars;
    string -> hash = hash;

    push(OBJ_VAL(string));
    hashMapSet(&vm.strings, string, NIL_VAL);
    pop();
    return string;
}

ObjString* copyString (const char* chars, int length) {
    uint32_t hash = hashString(chars, length);
    
    ObjString* interned = hashMapFindString(&vm.strings, chars, length, hash);

    if (interned != NULL) return interned;

    char* heapChars = ALLOCATE(char, length + 1);

    memcpy(heapChars, chars, length + 1);
    heapChars[length] = '\0';

    return allocateString(heapChars, length, hash);
}

ObjString* takeString(char* chars, int length) {
    uint32_t hash = hashString(chars, length);

    ObjString* interned = hashMapFindString(&vm.strings, chars, length, hash);

    if (interned != NULL) {
        FREE_ARRAY(char, chars, length + 1);
        return interned;
    }

    return allocateString(chars, length, hash);
}

static void printFunction (ObjFunction* func) {
    if (func -> name == NULL) {
        printf("<script>");
        return;
    }
    printf("<fn %s>", func -> name -> chars);
}

ObjFunction* newFunction () {
    ObjFunction* func = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
    func -> arity = 0;
    func -> upValuesCount = 0;
    func -> name = NULL;
    initChunk(&func -> chunk);
    return func;
}

ObjNativeFn* newNative (int arity, NativeFn cfunc) {
    ObjNativeFn* lfuncs = ALLOCATE_OBJ(ObjNativeFn, OBJ_NATIVE);
    lfuncs -> func = cfunc;
    lfuncs -> arity = arity;
    return lfuncs;
}

ObjClosure* newClosure (ObjFunction* func) {

    ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, func -> upValuesCount);
    
    for (int i = 0; i < func -> upValuesCount; i++) {
        upvalues[i] = NULL;
    }

    ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
    closure -> rawFunc = func;

    closure ->upvalueCount = func -> upValuesCount;
    closure -> upvalues = upvalues;
    
    return closure;
}

ObjUpvalue* newUpvalue (Value* slot) {
    ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
    upvalue ->location = slot;
    upvalue -> next = NULL;
    upvalue->closed = NIL_VAL;
    return upvalue;
}

ObjClass* newCLass (ObjString* name) {
    ObjClass* clas = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
    clas ->name = name;
    initHashMap(&clas->methods, 0);
    return clas;
}

ObjInstance* newInstance (ObjClass* clas) {
    ObjInstance* instance = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
    instance->clas = clas;
    initHashMap(&instance->fields, 0);
    return instance;
}

ObjBoundMethod* newBoundMethod (Value receiver, ObjClosure* method) {
    ObjBoundMethod* boundMethod = ALLOCATE_OBJ(ObjBoundMethod, OBJ_BOUND_METHOD);
    boundMethod->receiver = receiver;
    boundMethod->method = method;
    return boundMethod;
}

void printObject(Value value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
            break;
        case OBJ_FUNCTION:
            printFunction (AS_FUNCTION(value));
            break;
        case OBJ_NATIVE:
            printf("<native fn: %d args>", AS_FUNCTION(value) -> arity);
            break;
        case OBJ_CLOSURE:
            printFunction(AS_CLOSURE(value) -> rawFunc);
            break;
        case OBJ_UPVALUE:
            printf("upvalue");
            break;
        case OBJ_CLASS:
            printf("<class: %s>", AS_CLASS(value)->name->chars);
            break;
        case OBJ_INSTANCE:
            printf("<instance of class: %s>", AS_INSTANCE(value)->clas->name->chars);
            break;
        case OBJ_BOUND_METHOD: {
            printFunction(AS_BOUNDMETHOD(value)->method -> rawFunc);
        }
    }
}
