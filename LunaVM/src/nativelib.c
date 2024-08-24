#define _CRT_SECURE_NO_WARNINGS

#include <string.h>
#ifndef luna_native_lib
#define luna_native_lib

#include "nativelib.h"
#include "object.h"

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

typedef struct {
    GLFWwindow* window;
} WindowEntry;

#define MAX_WINDOWS 1024

static WindowEntry* windowTable[MAX_WINDOWS];
static int nextId = 1;

int addWindow(GLFWwindow* window) {
    int id = nextId++;
    if (id >= MAX_WINDOWS) {
        return -1; // Tabela cheia
    }
    windowTable[id] = malloc(sizeof(WindowEntry));
    windowTable[id]->window = window;
    return id;
}

GLFWwindow* getWindow(int id) {
    if (id < 1 || id >= MAX_WINDOWS || windowTable[id] == NULL) {
        return NULL;
    }
    return windowTable[id]->window;
}

void removeWindow(int id) {
    if (id < 1 || id >= MAX_WINDOWS || windowTable[id] == NULL) {
        return;
    }
    free(windowTable[id]);
    windowTable[id] = NULL;
}


Value clockNative(int argCount, Value* args)
{
	return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

Value inputNative(int argCount, Value* args)
{
    char buffer[256];
    
    if (fgets(buffer, sizeof(buffer), stdin) != NULL)
    {
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
        }
        
        len = strlen(buffer);

        char* heapBuffer = (char*)malloc(len + 1);
        if (heapBuffer == NULL) {
            return NULL_VAL;
        }

        strcpy(heapBuffer, buffer);

        ObjString* string = takeString(heapBuffer, (int)len);
        if (string == NULL) {
            free(heapBuffer);
            return NULL_VAL;
        }

        return OBJ_VAL(string);
    }

    return NULL_VAL;
}



Value openNative(int argCount, Value* args)
{
    if (!IS_STRING(args[0])) {
        return NULL_VAL;
    }

    const char* path = AS_CSTRING(args[0]);

    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        return NULL_VAL;
    }

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* buffer = (char*)malloc(fileSize + 1);

    if (buffer == NULL) {
        fclose(file);
        return NULL_VAL;
    }

    size_t bytesRead = fread(buffer, 1, fileSize, file);
    if (bytesRead != (size_t)fileSize) {
        free(buffer);
        fclose(file);
        return NULL_VAL;
    }

    buffer[fileSize] = '\0';

    fclose(file);

    ObjString* string = takeString(buffer, fileSize);
    free(buffer);

    return OBJ_VAL(string);
}

Value stringLengthNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        return NULL_VAL;
    }

    ObjString* string = AS_STRING(args[0]);
    return NUMBER_VAL(string->length);
}

Value toNumberNative(int argCount, Value* args)
{
    if (argCount != 1 || !IS_STRING(args[0])) {
        return NULL_VAL;
    }

    ObjString* string = AS_STRING(args[0]);
    const char* str = string->characters;


    char* end;
    double number = strtod(str, &end);


    if (end == str) {

        return NULL_VAL;
    }

    return NUMBER_VAL(number);
}

Value cosNative(int argCount, Value* args)
{
    if(!IS_NUMBER(args[0])) return NULL_VAL;
    
    return NUMBER_VAL(cos(AS_NUMBER(args[0])));
}
Value sinNative(int argCount, Value* args)
{
    if(!IS_NUMBER(args[0])) return NULL_VAL;

    return NUMBER_VAL(sin(AS_NUMBER(args[0])));
}
Value tanNative(int argCount, Value* args)
{
    if(!IS_NUMBER(args[0])) return NULL_VAL;

    return NUMBER_VAL(tan(AS_NUMBER(args[0])));
}
Value powNative(int argCount, Value* args)
{
    if(!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) return NULL_VAL;

    return NUMBER_VAL(pow(AS_NUMBER(args[0]), AS_NUMBER(args[1])));
}
Value sqrtNative(int argCount, Value* args)
{
    if(!IS_NUMBER(args[0])) return NULL_VAL;

    return NUMBER_VAL(sqrt(AS_NUMBER(args[0])));
}

Value charAtNative(int argCount, Value* args)
{
    if (!IS_STRING(args[0]) || !IS_NUMBER(args[1])) {
        return NULL_VAL;
    }

    ObjString* str = AS_STRING(args[0]);
    int index = (int)AS_NUMBER(args[1]);

    if (index < 0 || index >= str->length) {
        return NULL_VAL;
    }

    char* charBuffer = (char*)malloc(2);
    if (charBuffer == NULL) {
        return NULL_VAL;
    }

    charBuffer[0] = str->characters[index];
    charBuffer[1] = '\0';

    ObjString* resultString = takeString(charBuffer, 1);
    if (resultString == NULL) {
        free(charBuffer);
        return NULL_VAL;
    }

    return OBJ_VAL(resultString);
}

Value substrNative(int argCount, Value* args)
{
    if (!IS_STRING(args[0]) || !IS_NUMBER(args[1]) || !IS_NUMBER(args[2])) {
        return NULL_VAL;
    }

    ObjString* str = AS_STRING(args[0]);
    int start = (int)AS_NUMBER(args[1]);
    int end = (int)AS_NUMBER(args[2]);

    if (start < 0 || start >= (int)str->length || end < start || end > (int)str->length) {
        return NULL_VAL;
    }

    int length = end - start;
    char* substrBuffer = (char*)malloc(length + 1);
    if (substrBuffer == NULL) {
        return NULL_VAL;
    }

    memcpy(substrBuffer, str->characters + start, length);
    substrBuffer[length] = '\0';

    ObjString* resultString = takeString(substrBuffer, length);
    free(substrBuffer);

    if (resultString == NULL) {
        return NULL_VAL;
    }

    return OBJ_VAL(resultString);
}

Value writeNative(int argCount, Value* args) {
    if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
        return NULL_VAL;
    }

    const char* path = AS_CSTRING(args[0]);
    const char* content = AS_CSTRING(args[1]);

    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        return BOOL_VAL(false);
    }

    size_t contentLength = strlen(content);
    size_t bytesWritten = fwrite(content, 1, contentLength, file);

    fclose(file);

    if (bytesWritten != contentLength) {
        return BOOL_VAL(false);
    }

    return BOOL_VAL(true);
}

Value __glfwInit(int argCount, Value* args) {
    return BOOL_VAL(glfwInit());
}

Value __glfwCreateWindow(int argCount, Value* args) {
    if (argCount != 3 || !IS_NUMBER(args[0]) || !IS_NUMBER(args[1]) || !IS_STRING(args[2])) {
        return NULL_VAL;
    }

    int width = (int)AS_NUMBER(args[0]);
    int height = (int)AS_NUMBER(args[1]);
    const char* title = AS_CSTRING(args[2]);
    GLFWmonitor* monitor = (GLFWmonitor*)(intptr_t)AS_NUMBER(args[3]);

    GLFWwindow* window = glfwCreateWindow(width, height, title, monitor, NULL);

    if (!window) {
        printf("Window not created");
        return NULL_VAL;
    }

    double windowPtr = (double)(intptr_t)window;
    return NUMBER_VAL(windowPtr);
}

Value __glfwMakeContextCurrent(int argCount, Value* args) {
    if (argCount != 1 || !IS_NUMBER(args[0])) {
        return NULL_VAL;
    }

    GLFWwindow* window = (GLFWwindow*)(intptr_t)AS_NUMBER(args[0]);

    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        printf("Glad not initialized");
        return NULL_VAL;
    }

    return NULL_VAL;
}

Value __glfwWindowShouldClose(int argCount, Value* args) {
    if (argCount != 1 || !IS_NUMBER(args[0])) {
        return NULL_VAL;
    }

    GLFWwindow* window = (GLFWwindow*)(intptr_t)AS_NUMBER(args[0]);

    int shouldClose = glfwWindowShouldClose(window);

    return BOOL_VAL(shouldClose);
}

Value __glfwPollEvents(int argCount, Value* args) {
    if (argCount != 0) {
        return NULL_VAL;
    }

    glfwPollEvents();

    return NULL_VAL;
}

Value __glfwSwapBuffers(int argCount, Value* args) {
    if (argCount != 1 || !IS_NUMBER(args[0])) {
        return NULL_VAL;
    }

    GLFWwindow* window = (GLFWwindow*)(intptr_t)AS_NUMBER(args[0]);
    glfwSwapBuffers(window);
    return NULL_VAL;
}

Value __glClearColor(int argCount, Value* args) {
    if (argCount != 4 || !IS_NUMBER(args[0]) || !IS_NUMBER(args[1]) || !IS_NUMBER(args[2]) || !IS_NUMBER(args[3])) {
        return NULL_VAL;
    }

    float r = (float)AS_NUMBER(args[0]);
    float g = (float)AS_NUMBER(args[1]);
    float b = (float)AS_NUMBER(args[2]);
    float a = (float)AS_NUMBER(args[3]);

    glClearColor(r, g, b, a);
    return NULL_VAL;
}

Value __glClear(int argCount, Value* args) {
    if (argCount != 1 || !IS_NUMBER(args[0])) {
        return NULL_VAL;
    }

    GLenum mask = (GLenum)(int)AS_NUMBER(args[0]);
    glClear(mask);
    return NULL_VAL;
}

Value __gladLoadProc(int argCount, Value* args) {
    if (argCount != 1 || !IS_NUMBER(args[0])) {
        return NULL_VAL;
    }

    // Converte o valor double para um ponteiro de função
    GLADloadproc proc = (GLADloadproc)(intptr_t)AS_NUMBER(args[0]);
    return BOOL_VAL(gladLoadGLLoader(proc));
}

#endif
