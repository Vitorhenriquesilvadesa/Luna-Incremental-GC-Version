#include <stdio.h>
#include <string.h>

#include "lmemory.h"
#include "object.h"

#include <stdlib.h>

#include "value.h"
#include "vm.h"
#include "table.h"

#define ALLOCATE_OBJ(type, objectType) \
	(type*)allocateObject(sizeof(type), objectType)

static Obj* allocateObject(size_t size, ObjType type)
{
	Obj* object = (Obj*)reallocate(NULL, 0, size);
	object->type = type;
	object->isMarked = false;
	object->isOnCurrentGC = false;
	object->next = vm.objects;
	vm.objects = object;

#ifdef DEBUG_LOG_GC
	printf("%p allocate %zu for %d\n", (void*)object, size, type);
#endif

	return object;
}

ObjList* newList()
{
	ObjList* list = ALLOCATE_OBJ(ObjList, OBJ_LIST);
	list->elements = NULL;
	list->length = 0;
	return list;
}

void appendToList(ObjList* list, Value value) {
	list->length++;
	list->elements = (Value*)realloc(list->elements, sizeof(Value) * list->length);
	list->elements[list->length - 1] = value;
}

ObjBoundMethod* newBoundMethod(Value receiver, ObjClosure* method)
{
	ObjBoundMethod* bound = ALLOCATE_OBJ(ObjBoundMethod, OBJ_BOUND_METHOD);
	
	bound->receiver = receiver;
	bound->method = method;
	return bound;
}

ObjStruct* newStruct(ObjString* name)
{
	ObjStruct* klass = ALLOCATE_OBJ(ObjStruct, OBJ_STRUCT);
	klass->name = name;
	initTable(&klass->methods);
	return klass;
}

ObjUpvalue* newUpvalue(Value* slot)
{
	ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
	upvalue->location = slot;
	upvalue->closed = NULL_VAL;
	upvalue->next = NULL;
	return upvalue;
}

ObjClosure* newClosure(ObjFunction* function)
{
	ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);

	for (int i = 0; i < function->upvalueCount; i++)
	{
		upvalues[i] = NULL;
	}

	ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
	closure->function = function;
	closure->upvalues = upvalues;
	closure->upvalueCount = function->upvalueCount;

	return closure;
}

ObjFunction* newFunction()
{
	ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
	function->arity = 0;
	function->upvalueCount = 0;
	function->name = NULL;
	initChunk(&function->chunk);
	return function;
}

ObjInstance* newInstance(ObjStruct* klass)
{
	ObjInstance* instance = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
	instance->klass = klass;
	initTable(&instance->fields);
	return instance;
}

ObjNative* newNative(NativeFn function, uint8_t expectedArgCount)
{
	ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
	native->function = function;
	native->arity = expectedArgCount;
	return native;
}

static ObjString* allocateString(char* chars, int length, uint32_t hash)
{
	ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
	string->length = length;
	string->characters = chars;
	string->hash = hash;
	push(OBJ_VAL(string));
	tableSet(&vm.strings, string, NULL_VAL);
	pop();
	return string;
}

static uint32_t hashString(const char* key, int length)
{
	uint32_t hash = 2166136261u;

	for (int i = 0; i < length; i++)
	{
		hash ^= (uint8_t)key[i];
		hash *= 16777619;
	}

	return hash;
}

ObjString* takeString(char* characters, int length)
{
	uint32_t hash = hashString(characters, length);
	ObjString* intern = tableFindString(&vm.strings, characters, length, hash);

	if (intern != NULL)
	{
		FREE_ARRAY(char, characters, length + 1);
		return intern;
	}

	return allocateString(characters, length, hash);
}

ObjString* copyString(const char* characters, int length)
{
	uint32_t hash = hashString(characters, length);
	ObjString* intern = tableFindString(&vm.strings, characters, length, hash);

	if (intern != NULL) return intern;

	char* heapChars = ALLOCATE(char, length + 1);
	memcpy(heapChars, characters, length);
	heapChars[length] = '\0';


	return allocateString(heapChars, length, hash);
}

static void printFunction(ObjFunction* function)
{
	if (function->name == NULL)
	{
		printf("<script>");
		return;
	}

	printf("<fn %s>", function->name->characters);
}

void printObject(Value value)
{
	switch (OBJ_TYPE(value))
	{
	case OBJ_STRING:
		printf("%s", AS_CSTRING(value));
		break;

	case OBJ_FUNCTION:
		printFunction(AS_FUNCTION(value));
		break;

	case OBJ_INSTANCE:
		printf("<%s instance>", AS_INSTANCE(value)->klass->name->characters);
		break;

	case OBJ_NATIVE:
		printf("<native fn>");
		break;

	case OBJ_CLOSURE:
		printFunction(AS_CLOSURE(value)->function);
		break;

	case OBJ_UPVALUE:
		printf("<upvalue>");
		break;

	case OBJ_STRUCT:
		printf("<struct %s>", AS_STRUCT(value)->name->characters);
		break;

	case OBJ_BOUND_METHOD:
		printFunction(AS_BOUND_METHOD(value)->method->function);
		break;

	case OBJ_LIST:
		printf("<list>");
		break;
	}
}
