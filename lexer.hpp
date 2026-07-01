#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

enum class TokenType : uint8_t {
    // Symbols
    TK_EOF, TK_NAME, TK_NUMBER, TK_STRING,
    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_PERCENT, TK_CARET,
    TK_EQ, TK_NE, TK_LT, TK_GT, TK_LE, TK_GE,
    TK_ASSIGN, TK_LPAREN, TK_RPAREN, TK_LBRACK, TK_RBRACK,
    TK_LBRACE, TK_RBRACE, TK_DOT, TK_COMMA, TK_SEMI, TK_COLON,
    TK_DOTDOT, TK_ELLIPSIS,
    // Keywords
    TK_AND, TK_BREAK, TK_DO, TK_ELSE, TK_ELSEIF, TK_END,
    TK_FALSE, TK_FOR, TK_FUNCTION, TK_IF, TK_IN, TK_LOCAL,
    TK_NIL, TK_NOT, TK_OR, TK_REPEAT, TK_RETURN, TK_THEN,
    TK_TRUE, TK_UNTIL, TK_WHILE,
};

struct Token {
    TokenType type;
    std::string_view start;
    int length;
    int line;
};

class Lexer {
public:
    Lexer(const std::string& source);
    Token next();
    Token peekNext();
    bool isAtEnd() const;
    void skipToNextLine();

private:
    std::string source;
    int start = 0;
    int current = 0;
    int line = 1;

    char advance();
    char peek() const;
    char peekNextChar() const;
    bool match(char expected);
    void skipWhitespace();
    void skipComment();

    Token makeToken(TokenType type);
    Token errorToken(const char* msg);
    TokenType checkKeyword(int pos, int len, const char* rest, TokenType type);
    TokenType identifierType();
    Token string();
    Token number();
    Token identifier();
};
