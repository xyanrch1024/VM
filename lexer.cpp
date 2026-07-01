#include "lexer.hpp"
#include <cstring>
#include <cstdlib>

Lexer::Lexer(const std::string& s) : source(s) {}

bool Lexer::isAtEnd() const { return current >= (int)source.length(); }

char Lexer::advance() { return source[current++]; }

char Lexer::peek() const {
    return isAtEnd() ? '\0' : source[current];
}

char Lexer::peekNextChar() const {
    if (current + 1 >= (int)source.length()) return '\0';
    return source[current + 1];
}

bool Lexer::match(char expected) {
    if (isAtEnd() || source[current] != expected) return false;
    current++;
    return true;
}

Token Lexer::makeToken(TokenType type) {
    return {type, std::string_view(source.c_str() + start, current - start), current - start, line};
}

Token Lexer::errorToken(const char* msg) {
    return {TokenType::TK_EOF,
            std::string_view(msg, strlen(msg)),
            (int)strlen(msg), line};
}

void Lexer::skipWhitespace() {
    for (;;) {
        char c = peek();
        switch (c) {
            case ' ': case '\r': case '\t': advance(); break;
            case '\n': line++; advance(); break;
            case '-':
                if (peekNextChar() == '-') { skipComment(); break; }
                return;
            default: return;
        }
    }
}

void Lexer::skipComment() {
    advance(); advance(); // consume --
    while (!isAtEnd() && peek() != '\n') advance();
}

Token Lexer::string() {
    char quote = advance();
    while (!isAtEnd() && peek() != quote) {
        if (peek() == '\n') line++;
        else if (peek() == '\\') { advance(); }
        advance();
    }
    if (isAtEnd()) return errorToken("unfinished string");
    advance();

    // Extract content without quotes
    start++;
    current--;
    int len = current - start;
    current++;
    return {TokenType::TK_STRING,
            std::string_view(source.c_str() + start, len), len, line};
}

Token Lexer::number() {
    while (isdigit(peek())) advance();
    if (peek() == '.' && isdigit(peekNextChar())) {
        advance();
        while (isdigit(peek())) advance();
    }
    return makeToken(TokenType::TK_NUMBER);
}

TokenType Lexer::checkKeyword(int pos, int len, const char* rest, TokenType type) {
    if (current - start == pos + len &&
        memcmp(source.c_str() + start + pos, rest, len) == 0) {
        return type;
    }
    return TokenType::TK_NAME;
}

TokenType Lexer::identifierType() {
    if (source[start] == 'r' && current - start > 2) {
        switch (source[start+2]) {
            case 'p': return checkKeyword(2, 4, "peat", TokenType::TK_REPEAT);
            case 't': return checkKeyword(2, 4, "turn", TokenType::TK_RETURN);
        }
    }
    switch (source[start]) {
        case 'a': return checkKeyword(1, 2, "nd", TokenType::TK_AND);
        case 'b': return checkKeyword(1, 4, "reak", TokenType::TK_BREAK);
        case 'd': return checkKeyword(1, 1, "o", TokenType::TK_DO);
        case 'e':
            if (current - start == 1) return TokenType::TK_NAME;
            switch (source[start+1]) {
                    case 'l': return checkKeyword(2, 2, "se", TokenType::TK_ELSE);
                    case 'n': return checkKeyword(2, 2, "d", TokenType::TK_END);
            }
            break;
        case 'f':
            if (current - start > 1) {
                switch (source[start+1]) {
                        case 'a': return checkKeyword(2, 4, "lse", TokenType::TK_FALSE);
                        case 'o': return checkKeyword(2, 1, "r", TokenType::TK_FOR);
                        case 'u': return checkKeyword(2, 6, "nction", TokenType::TK_FUNCTION);
                }
            }
            break;
        case 'i':
            if (current - start > 1) {
                switch (source[start+1]) {
                        case 'f': return checkKeyword(2, 0, "", TokenType::TK_IF);
                        case 'n': return checkKeyword(2, 0, "", TokenType::TK_IN);
                }
            }
            break;
        case 'l': return checkKeyword(1, 4, "ocal", TokenType::TK_LOCAL);
        case 'n': return checkKeyword(1, 2, "il", TokenType::TK_NIL);
        case 'o': return checkKeyword(1, 1, "r", TokenType::TK_OR);

        case 't':
            if (current - start > 1) {
                switch (source[start+1]) {
                        case 'h': return checkKeyword(2, 2, "en", TokenType::TK_THEN);
                        case 'r': return checkKeyword(2, 3, "ue", TokenType::TK_TRUE);
                }
            }
            break;
        case 'u': return checkKeyword(1, 4, "ntil", TokenType::TK_UNTIL);
        case 'w': return checkKeyword(1, 4, "hile", TokenType::TK_WHILE);
    }
    return TokenType::TK_NAME;
}

Token Lexer::identifier() {
    while (isalnum(peek()) || peek() == '_') advance();
    return makeToken(identifierType());
}

Token Lexer::next() {
    skipWhitespace();
    start = current;
    if (isAtEnd()) return makeToken(TokenType::TK_EOF);
    char c = advance();
    if (isalpha(c) || c == '_') return identifier();
    if (isdigit(c)) return number();
    switch (c) {
        case '+': return makeToken(TokenType::TK_PLUS);
        case '-': return makeToken(TokenType::TK_MINUS);
        case '*': return makeToken(TokenType::TK_STAR);
        case '/': return makeToken(TokenType::TK_SLASH);
        case '%': return makeToken(TokenType::TK_PERCENT);
        case '^': return makeToken(TokenType::TK_CARET);
        case '(': return makeToken(TokenType::TK_LPAREN);
        case ')': return makeToken(TokenType::TK_RPAREN);
        case '[': return makeToken(TokenType::TK_LBRACK);
        case ']': return makeToken(TokenType::TK_RBRACK);
        case '{': return makeToken(TokenType::TK_LBRACE);
        case '}': return makeToken(TokenType::TK_RBRACE);
        case ',': return makeToken(TokenType::TK_COMMA);
        case ';': return makeToken(TokenType::TK_SEMI);
        case ':': return makeToken(TokenType::TK_COLON);
        case '.':
            if (match('.')) {
                if (match('.')) return makeToken(TokenType::TK_ELLIPSIS);
                return makeToken(TokenType::TK_DOTDOT);
            }
            return makeToken(TokenType::TK_DOT);
        case '=':
            if (match('=')) return makeToken(TokenType::TK_EQ);
            return makeToken(TokenType::TK_ASSIGN);
        case '~':
            if (match('=')) return makeToken(TokenType::TK_NE);
            break;
        case '<':
            if (match('=')) return makeToken(TokenType::TK_LE);
            return makeToken(TokenType::TK_LT);
        case '>':
            if (match('=')) return makeToken(TokenType::TK_GE);
            return makeToken(TokenType::TK_GT);
        case '"': case '\'': return string();
    }
    return errorToken("unexpected character");
}

Token Lexer::peekNext() {
    Token t = next();
    // Can't unread easily, so caller must manage
    return t;
}

void Lexer::skipToNextLine() {
    while (!isAtEnd() && peek() != '\n') advance();
    if (!isAtEnd()) { advance(); line++; }
}
