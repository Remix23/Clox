#include <stdio.h>
#include <string.h> 
#include <stdlib.h>
#include <stdbool.h>

#include "common.h"
#include "scanner.h"

typedef struct {
    const char* start;
    const char* current;
    int lenght;
    int line;
} Scanner;

Scanner scanner;

static Token makeToken (TokenType type) {
    Token token;

    token.ttype = type;
    token.start = scanner.start;
    token.length = (int) (scanner.current - scanner.start);
    token.line = scanner.line;
    return token;
}

static Token errorToken (const char* msg) {
    Token token;
    token.ttype = TOKEN_ERROR;
    token.start = msg;
    token.length = (int) strlen(msg);
    token.line = scanner.line;
    return token;
}

static bool isAtEnd() {
    return *scanner.current == '\0';
}

static char peek () {
    return *scanner.current;
}

static char peekNext () {
    if (isAtEnd()) return '\0';
    return scanner.current[1];
}

static bool isDigit (char c) {
    return c >= '0' && c <= '9';
}

static bool isAlpha (char c) {
    return (c >= 'a' && c <= 'z') || 
           (c >= 'A' && c <= 'Z') || 
           c == '_';

}

static char advance () {
    scanner.current++;
    return scanner.current[-1];
}

static bool match (char expected) {
    if (isAtEnd()) return false;
    if (*scanner.current != expected) return false;
    
    advance();
    return true;
}

static Token string () {
    while (peek() != '"' && !isAtEnd()) {
        if (peek() == '\n') scanner.line++;
        advance();
    }

    if (isAtEnd()) return errorToken("Unterminated string.");

    // closeing quote
    advance();
    return makeToken(TOKEN_STRING);
}

static Token number () {
    while (isDigit(peek())) advance();

    //frac part
    if (peek() == '.' && isDigit(peekNext())) {
        // consuming the '.'
        advance();

        while (isDigit(peek())) advance();
    }

    return makeToken(TOKEN_NUMBER);
}

static TokenType checkKeyword (int start, int lenght, const char* rest, TokenType type) {
    // if the lenght of the keyword is different from the lenght of the rest
    // then it's not a keyword
    if (scanner.current - scanner.start != start + lenght) return TOKEN_IDENTIFIER;

    // compare the string
    if (memcmp(scanner.start + start, rest, lenght) == 0) return type;

    return TOKEN_IDENTIFIER;
} 

static TokenType indentifyType () {

    switch (scanner.start[0]) {
        case 'a': return checkKeyword(1, 2, "nd", TOKEN_AND);
        case 'b': return checkKeyword(1, 4, "reak", TOKEN_BREAK);

        // TODO: add continue support
        case 'c':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'l': return checkKeyword(2, 3, "ass", TOKEN_CLASS);
                    case 'o': return checkKeyword(2, 6, "ntinue", TOKEN_CONTINUE);

                    default: break;
                }
            }
            break;
        
        case 'e': return checkKeyword(1, 3, "lse", TOKEN_ELSE);

        case 'f': 
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1])
                {
                case 'u': return checkKeyword(2, 1, "n", TOKEN_FUN);
                case 'a': return checkKeyword(2, 3, "lse", TOKEN_FALSE);
                case 'o': return checkKeyword(2, 1, "r", TOKEN_FOR);
                
                default:
                    break;
                }
            }
            break;

        case 'i': return checkKeyword(1, 1, "f", TOKEN_IF);
        case 'n': return checkKeyword(1, 2, "il", TOKEN_NIL);
        case 'o': return checkKeyword(1, 1, "r", TOKEN_OR);
        case 'p': return checkKeyword(1, 4, "rint", TOKEN_PRINT);
        case 'r': return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
        case 's': return checkKeyword(1, 4, "uper", TOKEN_SUPER);
        
        case 't': 
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1])
                {
                    case 'h': return checkKeyword(2, 2, "is", TOKEN_THIS);
                    case 'r': return checkKeyword(2, 2, "ue", TOKEN_TRUE);
                }
            }
            break;

        case 'v': return checkKeyword(1, 2, "ar", TOKEN_VAR);
        case 'w': return checkKeyword(1, 4, "hile", TOKEN_WHILE);
    }

    return TOKEN_IDENTIFIER;
}

static Token identifier () {
    while (isAlpha(peek()) || isDigit(peek())) advance();

    return makeToken(indentifyType());
}

static void skipWhitespace () {
    for (;;) {
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
                advance();
                break;
            
            case '/':
                if (peekNext() == '/') {
                    while (peek() != '\n' && !isAtEnd()) advance();
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}



void initScanner(const char* source) {
    scanner.start = source;
    scanner.current = source;
    scanner.line = 1;
}

Token scanToken () {
    skipWhitespace();
    scanner.start = scanner.current;

    if (isAtEnd()) return makeToken(TOKEN_EOF);

    char c = advance();

    if (isDigit(c)) return number ();
    if (isAlpha(c)) return identifier ();

    switch (c)
    {
    case '-': return makeToken(TOKEN_MINUS);
    case '+': return makeToken(TOKEN_PLUS);
    case '*': return makeToken(TOKEN_STAR);
    case '/': return makeToken(TOKEN_SLASH);

    case '(': return makeToken(TOKEN_LEFT_PAREN);
    case ')': return makeToken(TOKEN_RIGHT_PAREN);
    case '{': return makeToken(TOKEN_LEFT_BRACE);
    case '}': return makeToken(TOKEN_RIGHT_BRACE);

    case ';': return makeToken(TOKEN_SEMICOLON);
    case ':': return makeToken(TOKEN_COLON);
    case ',': return makeToken(TOKEN_COMMA);
    case '.': return makeToken(TOKEN_DOT);
    case '?': return makeToken(TOKEN_QUESTION_MARK);

    case '!':
        return makeToken(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
    case '=':
        return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
    case '<':
        return makeToken(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
    case '>':
        return makeToken(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);

    case '"': 
        return string();

    // whitespaces

    default:
        break;
    }

    return errorToken("Unexpected character.");
}