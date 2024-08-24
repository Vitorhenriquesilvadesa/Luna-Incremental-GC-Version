#include "compiler.h"
#include "lmemory.h"
#include "vm.h"
#include <stdlib.h>

#include <time.h>
#include <stdio.h>
#include "debug.h"

typedef enum {
    GC_MARK_PHASE,
    GC_SWEEP_PHASE,
    GC_IDLE_PHASE
} GCPhase;

static GCPhase gcPhase = GC_IDLE_PHASE;


#define GC_HEAP_GROW_FACTOR 1.5

static void freeObject(Obj* object);

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    vm.bytesAllocated += newSize - oldSize;

    if (newSize > oldSize) {
#ifdef DEBUG_LOG_GC
        printf("Allocating %d bytes of memory.\n", newSize);
#endif

#ifdef DEBUG_STRESS_GC
        collectGarbage();
#endif
        if (vm.bytesAllocated > vm.nextGC || gcPhase != GC_IDLE_PHASE) {
            collectGarbage();
    }
}

    if (newSize == 0) {
        if (pointer != NULL) {
            free(pointer);
            return NULL;
        }
    }

    void* result = realloc(pointer, newSize);
    if (result == NULL) exit(1);

    return result;
}

static int currentMarkIndex = 0;

void markRoots() {
    while (currentMarkIndex < (vm.stackTop - vm.stack)) {
        markValue(vm.stack[currentMarkIndex]);
        currentMarkIndex++;
        if (currentMarkIndex % 8 == 0) return;
    }

    currentMarkIndex = 0;
    while (currentMarkIndex < vm.frameCount) {
        markObject((Obj*)vm.frames[currentMarkIndex].closure);
        currentMarkIndex++;
        if (currentMarkIndex % 8 == 0) return;
    }

    ObjUpvalue* upvalue = vm.openUpvalues;
    while (upvalue != NULL) {
        markObject((Obj*)upvalue);
        upvalue = upvalue->next;
        currentMarkIndex++;
        if (currentMarkIndex % 8 == 0) return;
    }

    markTable(&vm.globals);
    markCompilerRoots();
    markObject((Obj*)vm.initString);

    gcPhase = GC_SWEEP_PHASE;
    currentMarkIndex = 0;
}


static void markArray(ValueArray* array)
{
    for (int i = 0; i < array->count; i++)
    {
        markValue(array->values[i]);
    }
}

static void blanckenObject(Obj* object)
{
#ifdef DEBUG_LOG_GC
	printf("%p blacken ", (void*)object);
	printValue(OBJ_VAL(object));
	printf("\n");
#endif

    switch (object->type)
    {
    case OBJ_BOUND_METHOD:
        {
            ObjBoundMethod* bound = (ObjBoundMethod*)object;
            markValue(bound->receiver);
            markObject((Obj*)bound->method);
            break;
        }
    case OBJ_STRUCT:
        {
            ObjStruct* klass = (ObjStruct*)object;
            markObject((Obj*)klass->name);
            markTable(&klass->methods);
            break;
        }

    case OBJ_INSTANCE:
        {
            ObjInstance* instance = (ObjInstance*)object;
            markObject((Obj*)instance->klass);
            markTable(&instance->fields);
            break;
        }

    case OBJ_CLOSURE:
        {
            ObjClosure* closure = (ObjClosure*)object;
            markObject((Obj*)closure->function);

            for (int i = 0; i < closure->upvalueCount; i++)
            {
                markObject((Obj*)closure->upvalues[i]);
            }
            break;
        }

    case OBJ_FUNCTION:
        {
            ObjFunction* function = (ObjFunction*)object;
            markObject((Obj*)function->name);
            markArray(&function->chunk.constants);
            break;
        }

    case OBJ_UPVALUE:
        {
            markValue(((ObjUpvalue*)object)->closed);
            break;
        }

    case OBJ_NATIVE:
    case OBJ_STRING:
        break;
    }
}

static void traceReferences()
{
    while (vm.grayCount > 0)
    {
        Obj* object = vm.grayStack[--vm.grayCount];
        blanckenObject(object);
    }
}

static void sweep()
{
    Obj* previous = NULL;
    Obj* object = vm.objects;

    while (object != NULL)
    {
        if (object->isMarked)
        {
            object->isMarked = false;
            object->isOnCurrentGC = false;
            previous = object;
            object = object->next;
        }
        else if (object->isOnCurrentGC)
        {
            Obj* unreached = object;
            object = object->next;

            if (previous != NULL)
            {
                previous->next = object;
            }
            else
            {
                vm.objects = object;
            }

            freeObject(unreached);
        }
        else
        {
            previous = object;
            object = object->next;
        }
    }
}


void collectGarbage() {
#ifdef DEBUG_LOG_GC_START_END
    printf("--gc begin \n");
    size_t before = vm.bytesAllocated;
    clock_t start_time = clock();
#endif

    vm.nextGC = (size_t)(((double)vm.bytesAllocated) * GC_HEAP_GROW_FACTOR);

    switch (gcPhase) {
    case GC_IDLE_PHASE:
        gcPhase = GC_MARK_PHASE;
        currentMarkIndex = 0;
        printf("Idle phase. Next phase at %zu\n", vm.nextGC);
        break;

    case GC_MARK_PHASE:
        markRoots();
        if (gcPhase != GC_MARK_PHASE) {
            gcPhase = GC_SWEEP_PHASE;
        }
        printf("Mark phase. Next phase at %zu\n", vm.nextGC);
        break;

    case GC_SWEEP_PHASE:
        printf("Sweep phase. Next phase at %zu\n", vm.nextGC);
        sweep();
        gcPhase = GC_IDLE_PHASE;

#ifdef DEBUG_LOG_GC_START_END
        clock_t end_time = clock();
        double time_spent = (double)(end_time - start_time) / CLOCKS_PER_SEC;

        printf("--gc end \n");
        printf("    now with %zu Bytes. Collected %zu bytes (from %zu to %zu) next at %zu\n",
            vm.bytesAllocated,
            before - vm.bytesAllocated,
            before,
            vm.bytesAllocated,
            vm.nextGC);
        printf("    GC took %.6f seconds\n", time_spent);
#endif
        break;
    }
}


void markValue(Value value)
{
    if (IS_OBJ(value)) markObject(AS_OBJ(value));
}

void markObject(Obj* object)
{
    if (object == NULL) return;
    if (object->isMarked) return;

#ifdef DEBUG_LOG_GC
	printf("%p mark ", (void*)object);
	printValue(OBJ_VAL(object));
	printf("\n");
#endif

    object->isMarked = true;
    object->isOnCurrentGC = true;

    if (vm.grayCapacity < vm.grayCount + 1)
    {
        vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
        vm.grayStack = (Obj**)realloc(vm.grayStack, sizeof(Obj*) * vm.grayCapacity);

        if (vm.grayStack == NULL) exit(1);
    }

    vm.grayStack[vm.grayCount++] = object;
}

static void freeObject(Obj* object)
{
#ifdef DEBUG_LOG_GC
	printf("%p free type %d\n", (void*)object, object->type);
#endif

    switch (object->type)
    {
    case OBJ_BOUND_METHOD:
        {
            FREE(ObjBoundMethod, object);
            break;
        }

    case OBJ_STRUCT:
        {
            ObjStruct* klass = (ObjStruct*)object;
            freeTable(&klass->methods);
            FREE(ObjStruct, object);
            break;
        }

    case OBJ_STRING:
        {
            ObjString* string = (ObjString*)object;
            FREE_ARRAY(char, string->characters, string->length + 1);
            FREE(ObjString, object);
            break;
        }

    case OBJ_UPVALUE:
        {
            FREE(ObjUpvalue, object);
            break;
        }

    case OBJ_CLOSURE:
        {
            ObjClosure* closure = (ObjClosure*)object;
            FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
            FREE(ObjClosure, object);
            break;
        }

    case OBJ_FUNCTION:
        {
            ObjFunction* function = (ObjFunction*)object;
            freeChunk(&function->chunk);
            FREE(ObjFunction, object);
            break;
        }

    case OBJ_INSTANCE:
        {
            ObjInstance* instance = (ObjInstance*)object;
            freeTable(&instance->fields);
            FREE(ObjInstance, object);
            break;
        }

    case OBJ_NATIVE:
        {
            FREE(ObjNative, object);
            break;
        }
    }
}

void freeObjects()
{
    Obj* object = vm.objects;

    while (object != NULL)
    {
        Obj* next = object->next;
        freeObject(object);
        object = next;
    }

    free(vm.grayStack);
}
