#include <stdlib.h>

#include "memory.h"
#include "object.h"
#include "vm.h"

void* reallocate (void* pointer, size_t oldSize, size_t newSize) {
    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, newSize);
    if (result == NULL) exit(1);
    return result;
}

static void freeObject (Obj* obj) {
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