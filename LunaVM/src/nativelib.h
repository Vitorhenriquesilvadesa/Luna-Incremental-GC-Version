#pragma once

#include "value.h"

Value clockNative(int argCount, Value* args);
Value inputNative(int argCount, Value* args);
Value openNative(int argCount, Value* args);
Value stringLengthNative(int argCount, Value* args);
Value toNumberNative(int argCount, Value* args);
Value cosNative(int argCount, Value* args);
Value sinNative(int argCount, Value* args);
Value tanNative(int argCount, Value* args);
Value powNative(int argCount, Value* args);
Value sqrtNative(int argCount, Value* args);
Value charAtNative(int argCount, Value* args);
Value substrNative(int argCount, Value* args);
Value writeNative(int argCount, Value* args);
Value __glfwInit(int argCount, Value* args);
Value __glfwCreateWindow(int argCount, Value* args);
Value __glfwMakeContextCurrent(int argCount, Value* args);
Value __glfwWindowShouldClose(int argCount, Value* args);
Value __glfwPollEvents(int argCount, Value* args);
Value __glfwSwapBuffers(int argCount, Value* args);
Value __glClearColor(int argCount, Value* args);
Value __glClear(int argCount, Value* args);
Value __gladLoadProc(int argCount, Value* args);