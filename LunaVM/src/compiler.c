#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"
#include "lmemory.h"

#ifdef DEBUG_PRINT_CODE

#include "debug.h"

#endif

typedef struct Module
{
    char* value;
    struct Module* next;
} Module;

Module* importedModulesHead = NULL;

typedef struct
{
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

typedef enum
{
    PREC_NONE,
    PREC_ASSIGNMENT, // =
    PREC_OR, // or
    PREC_AND, // and
    PREC_EQUALITY, // == !=
    PREC_COMPARISON, // < > <= >=
    PREC_TERM, // + -
    PREC_FACTOR, // * /
    PREC_UNARY, // ! -
    PREC_CALL, // . ()
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct
{
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

typedef struct
{
    Token name;
    int depth;
    bool isCaptured;
} Local;

typedef struct
{
    uint8_t index;
    bool isLocal;
} Upvalue;

typedef enum
{
    TYPE_FUNCTION,
    TYPE_SCRIPT,
    TYPE_IMPORT,
    TYPE_METHOD,
    TYPE_INITIALIZER,
} FunctionType;


typedef struct Compiler
{
    struct Compiler* enclosing;
    ObjFunction* function;
    FunctionType type;
    Local locals[UINT8_COUNT];
    int localCount;
    Upvalue upvalues[UINT8_COUNT];
    int scopeDepth;
} Compiler;

typedef struct StructCompiler
{
    struct StructCompiler* enclosing;
    bool hasSuperstruct;
} StructCompiler;

Parser parser;
Compiler* current = NULL;
StructCompiler* currentStruct = NULL;
Chunk* compilingChunk;
char* currentModuleName = NULL;

static int resolveUpvalue(Compiler* compiler, Token* name);

static Chunk* currentChunk()
{
    return &current->function->chunk;
}

static void errorAt(Token* token, const char* message)
{
    if (parser.panicMode) return;

    parser.panicMode = true;
    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF)
    {
        fprintf(stderr, " at end");
    }
    else if (token->type == TOKEN_ERROR)
    {
        // To implement
    }
    else
    {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s - ", message);
    fprintf(stderr, "in %s\n", currentModuleName);
    parser.hadError = true;
}

static void importError(Token* token, const char* moduleName)
{
    if (parser.panicMode) return;

    parser.panicMode = true;
    fprintf(stderr, "[line %d] Error at '%s': module '%s' already imported.\n",
            token->line, currentModuleName, moduleName);

    parser.hadError = true;
}

static void errorAtCurrent(const char* message)
{
    errorAt(&parser.current, message);
}

static void error(const char* message)
{
    errorAt(&parser.current, message);
}

static void advance()
{
    parser.previous = parser.current;

    for (;;)
    {
        parser.current = scanToken();
        if (parser.current.type != TOKEN_ERROR) break;

        errorAtCurrent(parser.current.start);
    }
}

static void consume(TokenType type, const char* message)
{
    if (parser.current.type == type)
    {
        advance();
        return;
    }
    errorAtCurrent(message);
}

static bool check(TokenType type)
{
    return parser.current.type == type;
}

static bool match(TokenType type)
{
    if (!check(type)) return false;
    advance();
    return true;
}

static void emitByte(uint8_t byte)
{
    writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2)
{
    emitByte(byte1);
    emitByte(byte2);
}

static void emitLoop(int loopStart)
{
    emitByte(OP_LOOP);

    int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) error("Loop body too large.");

    emitByte((offset >> 8) & 0xFF);
    emitByte(offset & 0xFF);
}

static int emitJump(uint8_t instruction)
{
    emitByte(instruction);
    emitByte(0xFF);
    emitByte(0xFF);
    return currentChunk()->count - 2;
}

static void emitReturn()
{
    if (current->type == TYPE_INITIALIZER)
    {
        emitBytes(OP_GET_LOCAL, 0);
    }
    else
    {
        emitByte(OP_NULL);
    }
    emitByte(OP_RETURN);
}

static uint8_t makeConstant(Value value)
{
    int constant = addConstant(currentChunk(), value);
    if (constant > UINT8_MAX)
    {
        error("Too many constant in one chunk.");
        return 0;
    }

    return (uint8_t)constant;
}

static void emitConstant(Value value)
{
    emitBytes(OP_CONSTANT, makeConstant(value));
}

static void patchJump(int offset)
{
    int jump = currentChunk()->count - offset - 2;

    if (jump > UINT16_MAX)
    {
        error("Too much code to jump over.");
    }

    currentChunk()->code[offset] = (jump >> 8) & 0xFF;
    currentChunk()->code[offset + 1] = jump & 0xFF;
}

static void initCompiler(Compiler* compiler, FunctionType type)
{
    compiler->enclosing = current;
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;

    if (type == TYPE_IMPORT)
    {
        compiler->function = current->function;
    }
    else
    {
        compiler->function = newFunction();
    }

    current = compiler;

    if (type != TYPE_SCRIPT)
    {
        current->function->name = copyString(parser.previous.start, parser.previous.length);
    }

    Local* local = &current->locals[current->localCount++];
    local->depth = 0;
    local->isCaptured = false;
    if (type != TYPE_FUNCTION && type != TYPE_IMPORT)
    {
        local->name.start = "self";
        local->name.length = 4;
    }
    else
    {
        local->name.start = "";
        local->name.length = 0;
    }
}

static ObjFunction* endCompiler()
{
    emitReturn();
    ObjFunction* function = current->function;

#ifdef DEBUG_PRINT_CODE
	if (!parser.hadError)
	{
		disassembleChunk(currentChunk(), function->name != NULL ? function->name->characters : "<script>");
	}
#endif

    current = current->enclosing;
    return function;
}

static void beginScope()
{
    current->scopeDepth++;
}

static void endScope()
{
    current->scopeDepth--;

    while (current->localCount > 0 && current->locals[current->localCount - 1].depth > current->scopeDepth)
    {
        emitByte(OP_POP);

        if (current->locals[current->localCount - 1].isCaptured)
        {
            emitByte(OP_CLOSE_UPVALUE);
        }
        else
        {
            emitByte(OP_POP);
        }

        current->localCount--;
    }
}

static void list(bool canAssign);
static void expression(void);
static void statement(void);
static void declaration(void);
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);
static uint8_t identifierConstant(Token* name);
static int resolveLocal(Compiler* compiler, Token* name);
static void declareVariable(void);

static uint8_t parseVariable(const char* errorMessage)
{
    consume(TOKEN_IDENTIFIER, errorMessage);

    declareVariable();
    if (current->scopeDepth > 0) return 0;

    return identifierConstant(&parser.previous);
}

static void markInitialized()
{
    if (current->scopeDepth == 0) return;
    current->locals[current->localCount - 1].depth = current->scopeDepth;
}

static void defineVariable(uint8_t global)
{
    if (current->scopeDepth > 0)
    {
        markInitialized();
        return;
    }

    emitBytes(OP_DEFINE_GLOBAL, global);
}

static uint8_t argumentList()
{
    uint8_t argCount = 0;

    if (!check(TOKEN_RIGHT_PAREN))
    {
        do
        {
            expression();
            if (argCount == 255)
            {
                error("Can't have more than 255 arguments.");
            }
            argCount++;
        }
        while (match(TOKEN_COMMA));
    }

    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return argCount;
}

static void and(bool canAssign)
{
    int endJump = emitJump(OP_JUMP_IF_FALSE);

    emitByte(OP_POP);
    parsePrecedence(PREC_AND);

    patchJump(endJump);
}

static void binary(bool canAssign)
{
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1));

    switch (operatorType)
    {
    case TOKEN_BANG_EQUAL: emitBytes(OP_EQUAL, OP_NOT);
        break;
    case TOKEN_EQUAL_EQUAL: emitByte(OP_EQUAL);
        break;
    case TOKEN_GREATER: emitByte(OP_GREATER);
        break;
    case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT);
        break;
    case TOKEN_LESS: emitByte(OP_LESS);
        break;
    case TOKEN_LESS_EQUAL: emitBytes(OP_GREATER, OP_NOT);
        break;
    case TOKEN_PLUS: emitByte(OP_ADD);
        break;
    case TOKEN_MINUS: emitByte(OP_SUBTRACT);
        break;
    case TOKEN_SLASH: emitByte(OP_DIVIDE);
        break;
    case TOKEN_STAR: emitByte(OP_MULTIPLY);
        break;
    case TOKEN_MOD: emitByte(OP_MOD);
        break;
    default: return; // Unreachable.
    }
}

static void call(bool canAssign)
{
    uint8_t argCount = argumentList();
    emitBytes(OP_CALL, argCount);
}

static void literal(bool canAssign)
{
    switch (parser.previous.type)
    {
    case TOKEN_FALSE: emitByte(OP_FALSE);
        break;
    case TOKEN_TRUE: emitByte(OP_TRUE);
        break;
    case TOKEN_NULL: emitByte(OP_NULL);
        break;
    default: return;
    }
}

static void grouping(bool canAssign)
{
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number(bool canAssign)
{
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}

static void or(bool canAssign)
{
    int elseJump = emitJump(OP_JUMP_IF_FALSE);
    int endJump = emitJump(OP_JUMP);

    patchJump(elseJump);
    emitByte(OP_POP);

    parsePrecedence(PREC_OR);
    patchJump(endJump);
}

static void string(bool canAssign)
{
    emitConstant(OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

static void namedVariable(Token name, bool canAssign)
{
    uint8_t getOp, setOp;
    int arg = resolveLocal(current, &name);

    if (arg != -1)
    {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    }
    else if ((arg = resolveUpvalue(current, &name)) != -1)
    {
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
    }
    else
    {
        arg = identifierConstant(&name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }

    if (canAssign && match(TOKEN_EQUAL))
    {
        expression();
        emitBytes(setOp, (uint8_t)arg);
    }
    else
    {
        emitBytes(getOp, (uint8_t)arg);
    }
}

static void variable(bool canAssign)
{
    namedVariable(parser.previous, canAssign);
}

static Token syntheticToken(const char* text)
{
    Token token;
    token.start = text;
    token.length = (int)strlen(text);
    return token;
}

static void super_(bool canAssign)
{
    if (currentStruct == NULL)
    {
        error("Can't use 'super' outside of struct.");
    }
    else if (!currentStruct->hasSuperstruct)
    {
        error("Can't use 'super' in leaf struct.");
    }

    consume(TOKEN_DOT, "Expect '.' after 'super'.");
    consume(TOKEN_IDENTIFIER, "Expect superstruct method name.");
    uint8_t name = identifierConstant(&parser.previous);

    namedVariable(syntheticToken("self"), false);

    if (match(TOKEN_LEFT_PAREN))
    {
        uint8_t argCount = argumentList();
        namedVariable(syntheticToken("super"), false);
        emitBytes(OP_SUPER_INVOKE, name);
        emitByte(argCount);
    }
    else
    {
        namedVariable(syntheticToken("super"), false);
        emitBytes(OP_GET_SUPER, name);
    }
}

static void unary(bool canAssign)
{
    TokenType operatorType = parser.previous.type;

    // Compile the operand.
    parsePrecedence(PREC_UNARY);

    // Emit operator instruction.
    switch (operatorType)
    {
    case TOKEN_BANG: emitByte(OP_NOT);
        break;
    case TOKEN_MINUS: emitByte(OP_NEGATE);
        break;
    default: return; // Unreachable
    }
}

static void dot(bool canAssign)
{
    consume(TOKEN_IDENTIFIER, "Expect properti name after '.'.");
    uint8_t name = identifierConstant(&parser.previous);

    if (canAssign && match(TOKEN_EQUAL))
    {
        expression();
        emitBytes(OP_SET_PROPERTY, name);
    }
    else if (match(TOKEN_LEFT_PAREN))
    {
        uint8_t argCount = argumentList();
        emitBytes(OP_INVOKE, name);
        emitByte(argCount);
    }
    else
    {
        emitBytes(OP_GET_PROPERTY, name);
    }
}

static void self(bool canAssign)
{
    if (currentStruct == NULL)
    {
        error("Cannot use 'self' out of struct.");
        return;
    }

    variable(false);
}

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN] = {grouping, call, PREC_CALL},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
    [TOKEN_DOT] = {NULL, dot, PREC_CALL},
    [TOKEN_MINUS] = {unary, binary, PREC_TERM},
    [TOKEN_PLUS] = {NULL, binary, PREC_TERM},
    [TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE},
    [TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},
    [TOKEN_STAR] = {NULL, binary, PREC_FACTOR},
    [TOKEN_MOD] = {NULL, binary, PREC_FACTOR},
    [TOKEN_BANG] = {unary, NULL, PREC_NONE},
    [TOKEN_BANG_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_EQUAL_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_GREATER] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE},
    [TOKEN_STRING] = {string, NULL, PREC_NONE},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE},
    [TOKEN_AND] = {NULL, and, PREC_AND},
    [TOKEN_STRUCT] = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE},
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
    [TOKEN_FUN] = {NULL, NULL, PREC_NONE},
    [TOKEN_IF] = {NULL, NULL, PREC_NONE},
    [TOKEN_NULL] = {literal, NULL, PREC_NONE},
    [TOKEN_OR] = {NULL, or, PREC_OR},
    [TOKEN_PRINT] = {NULL, NULL, PREC_NONE},
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
    [TOKEN_SUPER] = {super_, NULL, PREC_NONE},
    [TOKEN_THIS] = {self, NULL, PREC_NONE},
    [TOKEN_TRUE] = {literal, NULL, PREC_NONE},
    [TOKEN_VAR] = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
    [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF] = {NULL, NULL, PREC_NONE},
    [TOKEN_IMPORT] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACKET] = {list, NULL, PREC_NONE},
};

static void parsePrecedence(Precedence precedence)
{
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;

    if (prefixRule == NULL)
    {
        error("Expect expression.");
        return;
    }

    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);

    while (precedence <= getRule(parser.current.type)->precedence)
    {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
    }

    if (canAssign && match(TOKEN_EQUAL))
    {
        error("Invalid assignment target.");
    }
}

static uint8_t identifierConstant(Token* name)
{
    return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static bool identifiersEqual(Token* a, Token* b)
{
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler* compiler, Token* name)
{
    for (int i = compiler->localCount - 1; i >= 0; i--)
    {
        Local* local = &compiler->locals[i];
        if (identifiersEqual(name, &local->name))
        {
            if (local->depth == -1)
            {
                error("Can't read local variable in its own initializer.");
            }
            return i;
        }
    }

    return -1;
}

static int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal)
{
    int upvalueCount = compiler->function->upvalueCount;

    for (int i = 0; i < upvalueCount; i++)
    {
        Upvalue* upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal)
        {
            return i;
        }
    }

    if (upvalueCount == UINT8_COUNT)
    {
        error("Too many closure variables in function.");
        return 0;
    }

    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].index = index;

    return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Compiler* compiler, Token* name)
{
    if (compiler->enclosing == NULL) return -1;

    int local = resolveLocal(compiler->enclosing, name);

    if (local != -1)
    {
        compiler->enclosing->locals[local].isCaptured = true;
        return addUpvalue(compiler, (uint8_t)local, true);
    }

    int upvalue = resolveUpvalue(compiler->enclosing, name);

    if (upvalue != -1)
    {
        return addUpvalue(compiler, (uint8_t)upvalue, false);
    }

    return -1;
}

static void addLocal(Token name)
{
    if (current->localCount == UINT8_COUNT)
    {
        error("Too many local variables in function.");
        return;
    }

    Local* local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = -1;
    local->isCaptured = false;
}

static void declareVariable()
{
    if (current->scopeDepth == 0) return;

    Token* name = &parser.previous;

    for (int i = current->localCount - 1; i >= 0; i--)
    {
        Local* local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scopeDepth)
        {
            break;
        }

        if (identifiersEqual(name, &local->name))
        {
            error("Already a variable with self name in self scope.");
        }
    }

    addLocal(*name);
}

static ParseRule* getRule(TokenType type)
{
    return &rules[type];
}

static void expression()
{
    parsePrecedence(PREC_ASSIGNMENT);
}

static void block()
{
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF))
    {
        declaration();
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void function(FunctionType type)
{
    Compiler compiler;
    initCompiler(&compiler, type);
    beginScope();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");

    if (!check(TOKEN_RIGHT_PAREN))
    {
        do
        {
            current->function->arity++;
            if (current->function->arity > 255)
            {
                errorAtCurrent("Can't have more than 255 parameters.");
            }
            uint8_t constant = parseVariable("Expect parameter name.");
            defineVariable(constant);
        }
        while (match(TOKEN_COMMA));
    }

    consume(TOKEN_RIGHT_PAREN, "Expect ')' after function parameters.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");

    block();

    ObjFunction* function = endCompiler();
    emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));

    for (int i = 0; i < function->upvalueCount; i++)
    {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler.upvalues[i].index);
    }
}

static void method()
{
    consume(TOKEN_FUN, "Expect 'def' keyword to declare method.");
    consume(TOKEN_IDENTIFIER, "Expect function name.");
    uint8_t constant = identifierConstant(&parser.previous);

    FunctionType type = TYPE_METHOD;

    if (parser.previous.length == 4 && memcmp(parser.previous.start, "init", 4) == 0)
    {
        type = TYPE_INITIALIZER;
    }

    function(type);
    emitBytes(OP_METHOD, constant);
}

static void funDeclaration()
{
    uint8_t global = parseVariable("Expect function name.");
    markInitialized();
    function(TYPE_FUNCTION);
    defineVariable(global);
}

static void varDeclaration()
{
    uint8_t global = parseVariable("Expect variable name.");

    if (match(TOKEN_EQUAL))
    {
        expression();
    }
    else
    {
        emitByte(OP_NULL);
    }

    //consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

    defineVariable(global);
}

static void structDeclaration()
{
    if (current->scopeDepth > 0)
    {
        error("Cannot declare struct out of global scope.");
    }

    consume(TOKEN_IDENTIFIER, "Expect struct name.");
    Token structName = parser.previous;
    uint8_t nameConstant = identifierConstant(&parser.previous);
    declareVariable();

    emitBytes(OP_STRUCT, nameConstant);
    defineVariable(nameConstant);

    StructCompiler structCompiler;
    structCompiler.enclosing = currentStruct;
    structCompiler.hasSuperstruct = false;
    currentStruct = &structCompiler;

    if (match(TOKEN_COLON))
    {
        consume(TOKEN_IDENTIFIER, "Expect superstruct name.");
        variable(false);

        if (identifiersEqual(&structName, &parser.previous))
        {
            error("A struct can't copy behavior from itself.");
        }

        beginScope();
        addLocal(syntheticToken("super"));
        defineVariable(0);

        namedVariable(structName, false);

        emitByte(OP_INHERIT);
        structCompiler.hasSuperstruct = true;
    }

    namedVariable(structName, false);

    if (check(TOKEN_LEFT_BRACE))
    {
        consume(TOKEN_LEFT_BRACE, "Expect '{' before struct body.");
        while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF))
        {
            method();
        }
        consume(TOKEN_RIGHT_BRACE, "Expect '}' after struct body.");
    }
    else
    {
        consume(TOKEN_SEMICOLON, "Expect ';' after empty struct declaration.");
    }

    emitByte(OP_POP);

    if (structCompiler.hasSuperstruct)
    {
        endScope();
    }

    currentStruct = currentStruct->enclosing;
}

static void list(bool canAssign)
{
    int length = 0;
    ObjList* list = newList();

    if (!check(TOKEN_RIGHT_BRACKET))
    {
        do
        {
            if (length < 255) 
            {
                expression();
                emitByte(OP_ADD_LIST);
                length++;
            }
            else
            {
                error("Can't have more than 255 values in one list.");
            }
        } while (match(TOKEN_COMMA));

        consume(TOKEN_RIGHT_BRACKET, "Expect ']' at list values.");
    }

    emitConstant(OBJ_VAL(list));
}

static void addImportedModule(const char* moduleName)
{
    Module* newNode = malloc(sizeof(Module));
    if (newNode == NULL)
    {
        error("Memory allocation failed.");
        exit(1);
    }

    newNode->value = _strdup(moduleName);
    if (newNode->value == NULL)
    {
        error("Memory allocation failed.");
        exit(1);
    }
    newNode->next = importedModulesHead;
    importedModulesHead = newNode;
}

static bool isModuleImported(const char* moduleName)
{
    Module* current = importedModulesHead;
    while (current != NULL)
    {
        if (strcmp(current->value, moduleName) == 0)
        {
            return true;
        }
        current = current->next;
    }
    return false;
}

static void freeImportedModules()
{
    Module* current = importedModulesHead;
    while (current != NULL)
    {
        Module* next = current->next;
        free(current->value);
        free(current);
        current = next;
    }
    importedModulesHead = NULL;
}

static char* readFile(const char* path)
{
    FILE* file = fopen(path, "rb");
    if (file == NULL)
    {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        return NULL;
    }

    if (fseek(file, 0L, SEEK_END) != 0)
    {
        fprintf(stderr, "Could not determine the size of file \"%s\".\n", path);
        fclose(file);
        return NULL;
    }

    size_t fileSize = ftell(file);
    if (fileSize == (size_t)-1)
    {
        fprintf(stderr, "Could not determine the size of file \"%s\".\n", path);
        fclose(file);
        return NULL;
    }

    rewind(file);

    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL)
    {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        fclose(file);
        return NULL;
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    if (bytesRead < fileSize)
    {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        free(buffer);
        fclose(file);
        return NULL;
    }

    if (bytesRead >= 3 && (unsigned char)buffer[0] == 0xEF && (unsigned char)buffer[1] == 0xBB && (unsigned char)buffer[
        2] == 0xBF)
    {
        memmove(buffer, buffer + 3, bytesRead - 3);
        buffer[bytesRead - 3] = '\0';
    }
    else
    {
        buffer[bytesRead] = '\0';
    }

    if (fclose(file) != 0)
    {
        fprintf(stderr, "Error closing file \"%s\".\n", path);
        free(buffer);
        return NULL;
    }

    return buffer;
}

static void importModule(Token line, const char* name, int length)
{
    if (current->scopeDepth > 0)
    {
        switch (current->type)
        {
        case TYPE_METHOD:
            errorAt(&line, "Cannot import modules inside a method.");
            break;

        case TYPE_INITIALIZER:
            errorAt(&line, "Cannot import modules inside struct initializer.");
            break;

        case TYPE_FUNCTION:
            errorAt(&line, "Cannot import modules inside function.");
            break;

        case TYPE_IMPORT:
        case TYPE_SCRIPT:
            errorAt(&line, "Cannot import modules inside statement.");
            break;
        }

        return;
    }

    size_t start = 0;
    size_t end = length;

    if (length > 1 && name[0] == '\"' && name[length - 1] == '\"')
    {
        start = 1;
        end = length - 1;
    }

    size_t fileNameSize = end - start + 6;
    char* fileName = malloc(fileNameSize);
    if (fileName == NULL)
    {
        error("Memory allocation failed.");
        exit(1);
    }

    strncpy_s(fileName, fileNameSize, name + start, end - start);
    strcpy_s(fileName + (end - start), fileNameSize - (end - start), ".luna");

    if (isModuleImported(fileName))
    {
        importError(&line, fileName);
        free(fileName);
        return;
    }

    addImportedModule(fileName);

    char* previousModuleName = currentModuleName;

    currentModuleName = fileName;
    if (currentModuleName == NULL)
    {
        error("Memory allocation failed.");
        exit(1);
    }

    const char* source = readFile(fileName);

    Scanner previousScanner = scanner;
    initScanner(source);

    Parser previousParser = parser;
    Compiler* previousCompiler = current;

    Compiler compiler;

    initCompiler(&compiler, TYPE_IMPORT);

    advance();

    while (!match(TOKEN_EOF))
    {
        declaration();
    }

    parser = previousParser;
    current = previousCompiler;
    scanner = previousScanner;

    free(source);
    free(currentModuleName);

    currentModuleName = previousModuleName;
}

static void importDeclaration(void)
{
    consume(TOKEN_STRING, "Expect module name.");
    Token moduleName = parser.previous;
    importModule(moduleName, moduleName.start + 1, moduleName.length - 2);
}

static void expressionStatement()
{
    expression();
    //consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    emitByte(OP_POP);
}

static void forStatement()
{
    beginScope();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

    if (match(TOKEN_SEMICOLON))
    {
        // Empty initializer.
    }
    else if (match(TOKEN_VAR))
    {
        varDeclaration();
        consume(TOKEN_SEMICOLON, "Expect ';' after 'for' var declaration.");
    }
    else
    {
        expressionStatement();
        consume(TOKEN_SEMICOLON, "Expect ';' after 'for' expression clause.");
    }

    int loopStart = currentChunk()->count;
    int exitJump = -1;

    if (!match(TOKEN_SEMICOLON))
    {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

        exitJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP);
    }


    if (!match(TOKEN_RIGHT_PAREN))
    {
        int bodyJump = emitJump(OP_JUMP);
        int incrementStart = currentChunk()->count;
        expression();
        emitByte(OP_POP);
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after 'for' clauses.");

        emitLoop(loopStart);
        loopStart = incrementStart;
        patchJump(bodyJump);
    }

    statement();
    emitLoop(loopStart);

    if (exitJump != -1)
    {
        patchJump(exitJump);
        emitByte(OP_POP);
    }

    endScope();
}

static void ifStatement()
{
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();

    int elseJump = emitJump(OP_JUMP);

    patchJump(thenJump);
    emitByte(OP_POP);

    if (match(TOKEN_ELSE)) statement();
    patchJump(elseJump);
}

static void printStatement()
{
    expression();
    //consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emitByte(OP_PRINT);
}

static void printlnStatement()
{
    expression();
    //consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emitByte(OP_PRINTLN);
}

static void returnStatement()
{
    if (current->type == TYPE_SCRIPT)
    {
        error("Can't return from top level code.");
    }
    if (match(TOKEN_SEMICOLON))
    {
        emitReturn();
    }
    else
    {
        if (current->type == TYPE_INITIALIZER)
        {
            error("Cannot return a value from initializer.");
        }

        expression();
        //consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
        emitByte(OP_RETURN);
    }
}

static void whileStatement()
{
    int loopStart = currentChunk()->count;
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();
    emitLoop(loopStart);

    patchJump(exitJump);
    emitByte(OP_POP);
}

static void synchronize()
{
    parser.panicMode = false;

    while (parser.current.type != TOKEN_EOF)
    {
        if (parser.previous.type == TOKEN_SEMICOLON) return;

        switch (parser.current.type)
        {
        case TOKEN_STRUCT:
        case TOKEN_FUN:
        case TOKEN_VAR:
        case TOKEN_FOR:
        case TOKEN_IF:
        case TOKEN_WHILE:
        case TOKEN_PRINT:
        case TOKEN_RETURN:
            return;

        default:
            ; // Nothing
        }

        advance();
    }
}

static void declaration()
{
    if (match(TOKEN_SEMICOLON))
    {
        error("Unexpected token ';'.");
    }
    else if (match(TOKEN_IMPORT))
    {
        importDeclaration();
    }
    else if (match(TOKEN_STRUCT))
    {
        structDeclaration();
    }
    else if (match(TOKEN_FUN))
    {
        funDeclaration();
    }
    else if (match(TOKEN_VAR))
    {
        varDeclaration();
    }
    else
    {
        statement();
    }

    if (parser.panicMode) synchronize();
}

static void statement()
{
    if (match(TOKEN_SEMICOLON))
    {
        error("Unexpected token ';'.");
    }
    if (match(TOKEN_PRINT))
    {
        printStatement();
    }
    else if (match(TOKEN_PRINTLN))
    {
        printlnStatement();
    }
    else if (match(TOKEN_IF))
    {
        ifStatement();
    }
    else if (match(TOKEN_RETURN))
    {
        returnStatement();
    }
    else if (match(TOKEN_WHILE))
    {
        whileStatement();
    }
    else if (match(TOKEN_FOR))
    {
        forStatement();
    }
    else if (match(TOKEN_LEFT_BRACE))
    {
        beginScope();
        block();
        endScope();
    }
    else
    {
        expressionStatement();
    }
}

ObjFunction* compile(const char* filename, const char* source)
{
    currentModuleName = filename;
    initScanner(source);
    Compiler compiler;
    initCompiler(&compiler, TYPE_SCRIPT);

    parser.hadError = false;
    parser.panicMode = false;

    advance();

    while (!match(TOKEN_EOF))
    {
        declaration();
    }

    ObjFunction* function = endCompiler();
    return parser.hadError ? NULL : function;
}

void markCompilerRoots()
{
    Compiler* compiler = current;

    while (compiler != NULL)
    {
        markObject((Obj*)compiler->function);
        compiler = compiler->enclosing;
    }
}
