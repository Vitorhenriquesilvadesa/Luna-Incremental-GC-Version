#ifndef cluna_compiler_h
#define cluna_compiler_h

#include "object.h"
#include "vm.h"
#include "chunk.h"

ObjFunction* compile(const char* filename, const char* source);
void markCompilerRoots();

#endif