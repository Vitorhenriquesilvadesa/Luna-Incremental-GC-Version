#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "vm.h"

static void repl(void)
{
	for (;;)
	{
		char line[1024];
		printf("> ");

		if (!fgets(line, sizeof(line), stdin))
		{
			printf("\n");
			break;
		}

		if (strcmp(line, "exit\n") == 0)
		{
			printf("\n");
			break;
		}

		interpret("REPL", line);
	}
}

static char* readFile(const char* path)
{
	FILE* file = fopen(path, "rb");

	if (file == NULL)
	{
		fprintf(stderr, "Could not open file \"%s\".\n", path);
		exit(74);
	}

	fseek(file, 0L, SEEK_END);
	size_t fileSize = ftell(file);
	rewind(file);

	char* buffer = (char*)malloc(fileSize + 1);

	if (buffer == NULL)
	{
		fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
		exit(74);
	}

	size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);

	if (bytesRead < fileSize)
	{
		fprintf(stderr, "Could not read file \"%s\".\n", path);
		exit(74);
	}

	buffer[bytesRead] = '\0';

	int success = fclose(file);
	if (success != 0) printf("Erro ao fechar o arquivo\n\n\n");
	return buffer;
}

static void runFile(const char* path)
{
	char* source = readFile(path);

	InterpretResult result = interpret(path, source);
	free(source);

	if (result == INTERPRET_COMPILE_ERROR) exit(65);
	if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}

int main(int argc, const char* argv[]) {

	initVM();

	if (argc == 1)
	{
		repl();
	}
	else if (argc == 2)
	{
		// Internal commands if aplicable.
		if (strcmp(argv[1], "--version") == 0)
		{
			printf("Luna Version - 0.0.1 Debug\n");
			return 0;
		}

		runFile(argv[1]);
	}
	else
	{
		fprintf(stderr, "Usage: CLuna [path]\n");
		exit(64);
	}

	freeVM();

	return 0;
}