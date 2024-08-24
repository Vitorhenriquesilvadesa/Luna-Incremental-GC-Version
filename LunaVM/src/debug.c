#include <stdio.h>

#include "value.h"
#include "object.h"
#include "debug.h"

void disassembleChunk(Chunk* chunk, const char* name)
{
	printf("== %s == \n", name);

	for (int offset = 0; offset < chunk->count;) {
		offset = disassembleInstruction(chunk, offset);
	}
}

static int simpleInstruction(const char* name, int offset) 
{
	printf("%s\n", name);
	return offset + 1;
}

static int byteInstruction(const char* name, Chunk* chunk, int offset)
{
	uint8_t slot = chunk->code[offset + 1];
	printf("%-16s %4d \n", name, slot);
	return offset + 2;
}

static int jumpInstruction(const char* name, int sign, Chunk* chunk, int offset)
{
	uint16_t jump = (uint16_t)(chunk->code[offset + 1] << 8);
	jump |= chunk->code[offset + 2];
	printf("%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
	return offset + 3;
}

static int constantInstruction(const char* name, Chunk* chunk, int offset)
{
	uint8_t constant = chunk->code[offset + 1];
	printf("%-16s %4d '", name, constant);
	printValue(chunk->constants.values[constant]);
	printf("'\n");
	return offset + 2;
}

static int invokeInstruction(const char* name, Chunk* chunk, int offset)
{
	uint8_t constant = chunk->code[offset + 1];
	uint8_t argCount = chunk->code[offset + 2];
	printf("%-16s (%d args) %4d '", name, argCount, constant);
	printValue(chunk->constants.values[constant]);
	printf("'\n");
	return offset + 3;
}

int disassembleInstruction(Chunk* chunk, int offset)
{
	printf("%04d ", offset);

	if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1])
	{
		printf("  |  ");
	}
	else
	{
		printf("%4d ", chunk->lines[offset]);
	}

	uint8_t instruction = chunk->code[offset];
	switch (instruction) 
	{
	case OP_GET_PROPERTY:
		return constantInstruction("get_property", chunk, offset);

	case OP_SET_PROPERTY:
		return constantInstruction("set_property", chunk, offset);

	case OP_STRUCT:
		return constantInstruction("struct", chunk, offset);

	case OP_METHOD:
		return constantInstruction("method", chunk, offset);

	case OP_CONSTANT:
		return constantInstruction("push_constant", chunk, offset);

	case OP_NULL:
		return simpleInstruction("push_null", offset);

	case OP_TRUE:
		return simpleInstruction("push_true", offset);

	case OP_FALSE:	
		return simpleInstruction("push_false", offset);

	case OP_POP:
		return simpleInstruction("pop", offset);

	case OP_GET_LOCAL:
		return byteInstruction("get_local", chunk, offset);

	case OP_SET_LOCAL:
		return byteInstruction("set_local", chunk, offset);

	case OP_DEFINE_GLOBAL:
		return constantInstruction("define_global", chunk, offset);

	case OP_GET_GLOBAL:
		return constantInstruction("get_global", chunk, offset);

	case OP_SET_GLOBAL:
		return constantInstruction("set_global", chunk, offset);

	case OP_SET_UPVALUE:
		return byteInstruction("set_upvalue", chunk, offset);

	case OP_GET_UPVALUE:
		return byteInstruction("set_upvalue", chunk, offset);
	
	case OP_CLOSE_UPVALUE:
		return simpleInstruction("close_upvalue", offset);

	case OP_EQUAL:
		return simpleInstruction("op_equal", offset);

	case OP_GREATER:
		return simpleInstruction("op_greater", offset);

	case OP_LESS:
		return simpleInstruction("op_less", offset);

	case OP_NEGATE:
		return simpleInstruction("negate", offset);

	case OP_ADD:
		return simpleInstruction("add", offset);

	case OP_SUBTRACT:
		return simpleInstruction("sub", offset);

	case OP_MULTIPLY:
		return simpleInstruction("mul", offset);

	case OP_DIVIDE:
		return simpleInstruction("div", offset);

	case OP_NOT:
		return simpleInstruction("not", offset);

	case OP_PRINT:
		return simpleInstruction("print", offset);

	case OP_PRINTLN:
		return simpleInstruction("println", offset);

	case OP_JUMP:
		return jumpInstruction("jump", 1, chunk, offset);

	case OP_LOOP:
		return jumpInstruction("loop", -1, chunk, offset);

	case OP_JUMP_IF_FALSE:
		return jumpInstruction("jump_if_false", 1, chunk, offset);

	case OP_CALL:
		return byteInstruction("call", chunk, offset);

	case OP_CLOSURE:
	{
		offset++;
		uint8_t constant = chunk->code[offset++];
		printf("%-16s %4d ", "closure", constant);
		printValue(chunk->constants.values[constant]);
		printf("\n");

		ObjFunction* function = AS_FUNCTION(chunk->constants.values[constant]);

		for (int j = 0; j < function->upvalueCount; j++)
		{
			int isLocal = chunk->code[offset++];
			int index = chunk->code[offset++];
			printf("%04d  |  %s %d\n", offset - 2, isLocal ? "local" : "upvalue", index);
		}

		return offset;
	}

	case OP_INHERIT:
	{
		return simpleInstruction("inherit", offset);
	}

	case OP_INVOKE:
	{
		return invokeInstruction("invoke", chunk, offset);
	}

	case OP_SUPER_INVOKE:
	{
		return invokeInstruction("super_invoke", chunk, offset);
	}

	case OP_GET_SUPER:
	{
		return constantInstruction("get_super", chunk, offset);
	}

	case OP_RETURN:
		return simpleInstruction("return", offset);

	default:
		printf("Unknown opcode %d\n", instruction);
		return offset + 1;
	}

	return 0;
}
