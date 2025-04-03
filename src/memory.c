#include <stdlib.h>

#include "common.h"
#include "memory.h"
#include "object.h"
#include "compiler.h"
#include "vm.h"

#define GC_HEAP_GROW_FACTOR 2

void* reallocate (void* pointer, size_t oldSize, size_t newSize) {
    vm.bytesAlocated += newSize - oldSize;

    if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
        collectGarbage();
#endif

        if (vm.bytesAlocated > vm.nextGC) {
            collectGarbage();
        }
    }

    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, newSize);

    if (result == NULL) exit(1);
    return result;
}

static void freeObject (Obj* obj) {

#ifdef DEBUG_LOG_GC
    printf("%p free type %d\n", (void*)obj, obj -> otype);
#endif
    switch (obj->otype)
    {
    case OBJ_STRING: {
        ObjString* string = (ObjString*)obj;
        FREE_ARRAY(char, string->chars, string->length + 1);
        FREE(ObjString, obj);
        break;
    }
    case OBJ_FUNCTION: {
        ObjFunction* func = (ObjFunction*) obj;
        freeChunk(&func -> chunk);
        FREE(ObjFunction, func);
        break;
    }
    case OBJ_NATIVE: {
        FREE(ObjNativeFn, obj);
        break;
    }
    case OBJ_CLOSURE: {
        ObjClosure* closure = (ObjClosure*) obj;
        FREE_ARRAY(ObjUpvalue*, closure ->upvalues, closure ->upvalueCount);
        FREE(ObjClosure, obj); // since closure does not own the raw function
        break;
    }

    case OBJ_UPVALUE: {
        FREE(ObjUpvalue, obj);
        break;
    }

    case OBJ_CLASS: {
        ObjClass* clas = (ObjClass*) obj;
        freeHashMap(&clas->methods);
        FREE(ObjClass, obj);
        break;
    }
    case OBJ_INSTANCE: {
        ObjInstance* instance = (ObjInstance*) obj;
        freeHashMap(&instance->fields);
        FREE(ObjInstance, obj);
        break;
    }
    case OBJ_BOUND_METHOD: {
        FREE(ObjBoundMethod, obj);
        break;
    }

    default:
        break;
    }
}

void freeObjects () {
    Obj* object = vm.objects;
    while (object != NULL) {
        Obj* next = object->next;
        freeObject(object);
        object = next;
    }
}

void markValue (Value value) {
    if (IS_OBJ(value)) markObject(AS_OBJ(value));
}

void markObject (Obj* obj) {
    if (obj == NULL) return;
    if (obj->isMarked) return;

#ifdef DEBUG_LOG_GC
    printf("%p mark ", (void*) obj);
    printValue(OBJ_VAL(obj));
    printf("\n");
#endif

    obj -> isMarked = true;

    if (vm.grayCapacity < vm.grayCount + 1) {
        vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
        vm.grayStack = (Obj**)realloc(vm.grayStack, vm.grayCapacity * sizeof(Obj*));

        if (vm.grayStack == NULL) exit(1);
    }

    vm.grayStack[vm.grayCount++] = obj;
}

static void markArray (ValueArray* array) {
    for (int i = 0; i < array->count; i++) {
        markValue(array->values[i]);
    }
}

static void markRoots () {
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
        markValue(*slot);
    }

    for (int i = 0; i < vm.frameCount; i++) {
        markObject((Obj*)vm.frames[i].closure);
    }

    for (ObjUpvalue* upvalue = vm.openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
        markObject((Obj*) upvalue);
    }
    markCompilerRoots();
    markObject((Obj*)vm.initString);
    markHashMap(&vm.globals);
}

static void blackenObject (Obj* obj) {

#ifdef DEBUG_LOG_GC
    printf("%p blacken ", (void*) obj);
    printValue(OBJ_VAL(obj));
    printf("\n");
#endif

    switch (obj->otype)
    {
    case OBJ_STRING:
    case OBJ_NATIVE:
        break;
    case OBJ_UPVALUE:
        markValue(((ObjUpvalue*) obj)->closed);
        break;

    case OBJ_FUNCTION: {
        ObjFunction* func = (ObjFunction*) obj;
        markObject((Obj*)func->name);
        markArray(&func->chunk.constants);
        break;
    }
    case OBJ_CLOSURE: {
        ObjClosure* closure = (ObjClosure*) obj;
        markObject((Obj*)closure->rawFunc);
        for (int i = 0; i < closure->upvalueCount; i++) {
            markObject((Obj*)closure->upvalues[i]);
        }
        break;
    }
    case OBJ_CLASS: {
        ObjClass* clas = (ObjClass*) obj;
        markObject((Obj*) clas->name);
        markHashMap(&clas->methods);
        break;
    }
    case OBJ_INSTANCE: {
        ObjInstance* instance = (ObjInstance*) obj;
        markHashMap(&instance->fields);
        markObject((Obj*) instance->clas);
        break;
    }
    case OBJ_BOUND_METHOD: {
        ObjBoundMethod* boundMethod = (ObjBoundMethod*) obj;
        markValue(boundMethod->receiver);
        markObject((Obj*) boundMethod->method);
        break;
    }
    default:
        break;
    }
}

static void traceReferences () {
    while (vm.grayCount > 0) {
        Obj* obj = vm.grayStack[--vm.grayCount];
        blackenObject (obj);
    }
}

static void sweep () {
    Obj* previous = NULL;
    Obj* curr = vm.objects;
    while (curr != NULL) {

        if (!curr->isMarked) {
            Obj* to_free = curr;
            curr = curr -> next;
            if (previous == NULL) {
                vm.objects = curr;
            } else {
                previous->next = curr;
            }
            freeObject(to_free);
            continue;
        }

        curr->isMarked = false;
        previous = curr;
        curr = curr -> next;
    }
}

void collectGarbage () {
#ifdef DEBUG_LOG_GC
    printf("-- gc begin\n");
#endif

    // mark and sweep implementation

    size_t before = vm.bytesAlocated;

    markRoots();
    traceReferences();
    hashMapRemoveWhite(&vm.strings);
    sweep();

    vm.nextGC = vm.bytesAlocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
    printf("-- gc end\n");
    printf("   collected %zu bytes (from %zu to %zu) next at %zu\n", before - vm.bytesAlocated, before, vm.bytesAlocated, vm.nextGC);
#endif
}
