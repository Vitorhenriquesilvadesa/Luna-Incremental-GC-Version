#ifndef luna_object_h
#define luna_object_h

#include "common.h"
#include "value.h"
#include "chunk.h"
#include "table.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_LIST(value) isObjType(value, OBJ_LIST)
#define IS_CLOSURE(value) isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_NATIVE(value) isObjType(value, OBJ_NATIVE)
#define IS_STRING(value) isObjType(value, OBJ_STRING)
#define IS_STRUCT(value) isObjType(value, OBJ_STRUCT)
#define IS_INSTANCE(value) isObjType(value, OBJ_INSTANCE)
#define IS_BOUND_METHOD(value) isObjType(value, OBJ_BOUND_METHOD)

#define AS_LIST(value) ((ObjList*) AS_OBJ(value))
#define AS_STRUCT(value) ((ObjStruct*) AS_OBJ(value))
#define AS_INSTANCE(value) ((ObjInstance*) AS_OBJ(value))
#define AS_BOUND_METHOD(value) ((ObjBoundMethod*) AS_OBJ(value))
#define AS_CLOSURE(value) ((ObjClosure*) AS_OBJ(value))
#define AS_FUNCTION(value) ((ObjFunction*) AS_OBJ(value))
#define AS_STRING(value) ((ObjString*) AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*) AS_OBJ(value))->characters)
#define AS_NATIVE(value) (((ObjNative*)AS_OBJ(value))->function)
#define AS_NATIVE_FN(value) ((ObjNative*)AS_OBJ(value))

typedef enum {
	OBJ_STRING,
	OBJ_FUNCTION,
	OBJ_NATIVE,
	OBJ_CLOSURE,
	OBJ_UPVALUE,
	OBJ_STRUCT,
	OBJ_INSTANCE,
	OBJ_BOUND_METHOD,
	OBJ_LIST,
} ObjType;

struct Obj
{
	ObjType type;
	bool isMarked;
	bool isOnCurrentGC;
	struct Obj* next;
};

typedef struct
{
	Obj obj;
	int arity;
	int upvalueCount;
	Chunk chunk;
	ObjString* name;
} ObjFunction;

typedef Value(*NativeFn)(int argCount, Value* args);

typedef struct
{
	Obj obj;
	NativeFn function;
	uint8_t arity;
} ObjNative;

struct ObjString
{
	Obj obj;
	int length;
	char* characters;
	uint32_t hash;
};

typedef struct ObjUpvalue
{
	Obj obj;
	Value* location;
	Value closed;
	struct ObjUpvalue* next;
} ObjUpvalue;

typedef struct
{
	Obj obj;
	ObjFunction* function;
	ObjUpvalue** upvalues;
	int upvalueCount;
} ObjClosure;

typedef struct
{
	Obj obj;
	ObjString* name;
	Table methods;
} ObjStruct;

typedef struct
{
	Obj obj;
	ObjStruct* klass;
	Table fields;
} ObjInstance;

typedef struct
{
	Obj obj;
	Value receiver;
	ObjClosure* method;
} ObjBoundMethod;

typedef struct
{
	Obj obj;
	Value* elements;
	uint8_t length;
} ObjList;

ObjList* newList();
void appendToList(ObjList* list, Value value);

ObjBoundMethod* newBoundMethod(Value receiver, ObjClosure* method);
ObjStruct* newStruct(ObjString* name);
ObjUpvalue* newUpvalue(Value* slot);
ObjClosure* newClosure(ObjFunction* function);
ObjFunction* newFunction();
ObjInstance* newInstance(ObjStruct* klass);
ObjNative* newNative(NativeFn function, uint8_t expectedArgCount);

ObjString* takeString(char* characters, int length);
ObjString* copyString(const char* characters, int length);
void printObject(Value value);

static inline bool isObjType(Value value, ObjType type)
{
	return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif