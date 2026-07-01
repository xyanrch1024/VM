#pragma once
#include "ast.hpp"
#include <vector>
#include <string>

struct ParseError {};

class Parser {
public:
    Parser(const std::string& source);
    std::vector<Stmt*> parse();

private:
    Lexer lexer;
    Token current;
    Token previous;
    bool hadError = false;
    bool panicMode = false;

    void advance();
    void consume(TokenType type, const char* msg);
    bool check(TokenType type);
    bool match(TokenType type);
    void synchronize();

    // Grammar rules
    std::vector<Stmt*> program();
    Stmt* statement();
    Stmt* localDecl();
    Stmt* assignStmt();
    Stmt* ifStmt();
    Stmt* whileStmt();
    Stmt* repeatStmt();
    Stmt* returnStmt();
    Stmt* block();
    std::vector<Stmt*> blockStmts();

    Expr* expression();
    Expr* orExpr();
    Expr* andExpr();
    Expr* cmpExpr();
    Expr* concatExpr();
    Expr* addExpr();
    Expr* mulExpr();
    Expr* powExpr();
    Expr* unaryExpr();
    Expr* postfixExpr();
    Expr* prefixExpr();
    Expr* primaryExpr();
    Expr* tableConstructor();
    Expr* funcDef();
};
