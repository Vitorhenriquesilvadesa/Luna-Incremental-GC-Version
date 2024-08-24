#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"

void initScanner(const char* source)
{
	scanner.start = source;
	scanner.current = source;
	scanner.line = 1;
}

static bool isAlpha(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool isDigit(char c)
{
	return c >= '0' && c <= '9';
}

static bool isAtEnd()
{
	return *scanner.current == '\0';
}

static char advance()
{
	scanner.current++;
	return scanner.current[-1];
}

static char peek()
{
	return *scanner.current;
}

static char peekNext()
{
	return scanner.current[1];
}

static bool match(char expected)
{
	if (isAtEnd()) return false;
	if (*scanner.current != expected) return false;
	scanner.current++;
	return true;
}

static Token makeToken(TokenType type)
{
	Token token;
	token.type = type;
	token.start = scanner.start;
	token.length = (int)(scanner.current - scanner.start);
	token.line = scanner.line;
	return token;
}

static Token errorToken(const char* message)
{
	Token token;
	token.type = TOKEN_ERROR;
	token.start = message;
	token.length = (int)strlen(message);
	token.line = scanner.line;
	return token;
}

static void skipWhitespace()
{
	for (;;)
	{
		char c = peek();
		switch (c)
		{
		case ' ':
		case '\r':
		case '\t':
			advance();
			break;

		case '\n':
			scanner.line++;
			makeToken(TOKEN_NEWLINE);
			advance();
			break;

		case '#':
			while (peek() != '\n' && !isAtEnd()) advance();
			break;

		default:
			return;
		}
	}
}

static TokenType checkKeyword(int start, int length, const char* rest, TokenType type)
{
	if (scanner.current - scanner.start == start + length &&
		memcmp(scanner.start + start, rest, length) == 0)
	{
		return type;
	}

	return TOKEN_IDENTIFIER;
}

static bool isKeyword(int start, int length, const char* rest)
{
	return (scanner.current - scanner.start == start + length &&
		memcmp(scanner.start + start, rest, length) == 0);
}


static TokenType identifierType()
{
	switch (scanner.start[0])
	{
	case 'a': return checkKeyword(1, 2, "nd", TOKEN_AND);
	case 'd': return checkKeyword(1, 2, "ef", TOKEN_FUN);
	case 'e': return checkKeyword(1, 3, "lse", TOKEN_ELSE);
	case 'f':
		if (scanner.current - scanner.start > 1)
		{
			switch (scanner.start[1])
			{
			case 'a': return checkKeyword(2, 3, "lse", TOKEN_FALSE);
			case 'o': return checkKeyword(2, 1, "r", TOKEN_FOR);
			}
		}
		break;
	case 'i':
		if (scanner.current - scanner.start > 1)
		{
			switch (scanner.start[1])
			{
			case 'f': return checkKeyword(2, 0, "", TOKEN_IF);
			case 'm': return checkKeyword(2, 4, "port", TOKEN_IMPORT);
			}
		}
		break;
	case 'n': return checkKeyword(1, 3, "ull", TOKEN_NULL);
	case 'o': return checkKeyword(1, 1, "r", TOKEN_OR);
	case 'p':
		if (scanner.current - scanner.start >= 5 &&
			memcmp(scanner.start + 1, "rint", 4) == 0)
		{
			if (scanner.current - scanner.start == 5) {
				return TOKEN_PRINT;
			}
			if (scanner.current - scanner.start == 7 &&
				memcmp(scanner.start + 5, "ln", 2) == 0) {
				return TOKEN_PRINTLN;
			}
		}
		break;
	case 'r': return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
	case 's':
		if (scanner.current - scanner.start > 1)
		{
			switch (scanner.start[1])
			{
			case 'u': return checkKeyword(2, 3, "per", TOKEN_SUPER);
			case 'e': return checkKeyword(2, 2, "lf", TOKEN_THIS);
			case 't': return checkKeyword(2, 4, "ruct", TOKEN_STRUCT);
			}
		}
		break;
	case 't':
		if (scanner.current - scanner.start > 1)
		{
			switch (scanner.start[1])
			{
			case 'r': return checkKeyword(2, 2, "ue", TOKEN_TRUE);
			}
		}
		break;
	case 'v': return checkKeyword(1, 2, "ar", TOKEN_VAR);
	case 'w': return checkKeyword(1, 4, "hile", TOKEN_WHILE);
	}

	return TOKEN_IDENTIFIER;
}

static Token identifier()
{
	while (isAlpha(peek()) || isDigit(peek())) advance();
	return makeToken(identifierType());
}

static Token number()
{
	while (isDigit(peek())) advance();

	if (peek() == '.' && isDigit(peekNext()))
	{
		advance();
		while (isDigit(peek())) advance();
	}

	return makeToken(TOKEN_NUMBER);
}

static Token string()
{
	while (peek() != '"' && !isAtEnd())
	{
		if (peek() == '\n') scanner.line++;
		advance();
	}

	if (isAtEnd()) return errorToken("Unterminated string.");

	advance();
	return makeToken(TOKEN_STRING);
}

Token scanToken()
{
	skipWhitespace();
	scanner.start = scanner.current;

	if (isAtEnd())
	{
		return makeToken(TOKEN_EOF);
	}

	char c = advance();

	if (isAlpha(c)) return identifier();
	if (isDigit(c)) return number();

	switch (c)
	{
	case '(': return makeToken(TOKEN_LEFT_PAREN);
	case ':': return makeToken(TOKEN_COLON);
	case ')': return makeToken(TOKEN_RIGHT_PAREN);
	case '{': return makeToken(TOKEN_LEFT_BRACE);
	case '}': return makeToken(TOKEN_RIGHT_BRACE);
	case ';': return makeToken(TOKEN_SEMICOLON);
	case ',': return makeToken(TOKEN_COMMA);
	case '.': return makeToken(TOKEN_DOT);
	case '-': return makeToken(TOKEN_MINUS);
	case '+': return makeToken(TOKEN_PLUS);
	case '/': return makeToken(TOKEN_SLASH);
	case '*': return makeToken(TOKEN_STAR);
	case '%': return makeToken(TOKEN_MOD);
	case '[': return makeToken(TOKEN_LEFT_BRACKET);
	case ']': return makeToken(TOKEN_RIGHT_BRACKET);

	case '!':
		return makeToken(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);

	case '=':
		return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);

	case '>':
		return makeToken(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);

	case '<':
		return makeToken(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);

	case '"': return string();
	}

	return errorToken("Unexpected character.");
}
