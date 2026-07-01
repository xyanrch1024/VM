#pragma once
#include "lexer.hpp"
#include <vector>
#include <string>

struct Expr;
struct Stmt;

enum class ExprType : uint8_t {
    NIL, BOOL, NUMBER, STRING,
    NAME,
    UNARY, BINARY,
    CALL,
    FUNCDEF,
};

struct UnaryData  { TokenType op; Expr* rhs; };
struct BinaryData { TokenType op; Expr* lhs; Expr* rhs; };
struct CallData   { Expr* callee; std::vector<Expr*> args; };
struct FuncDefData { std::vector<const char*> params; Stmt* body; };

struct Expr {
    ExprType type;
    int line;
    union {
        bool boolVal;
        double numVal;
        const char* strVal;
    };
    void* data = nullptr;

    static Expr* makeNil(int line);
    static Expr* makeBool(int line, bool v);
    static Expr* makeNum(int line, double v);
    static Expr* makeStr(int line, const char* v);
    static Expr* makeName(int line, const char* v);
    static Expr* makeUnary(int line, TokenType op, Expr* rhs);
    static Expr* makeBinary(int line, TokenType op, Expr* lhs, Expr* rhs);
    static Expr* makeCall(int line, Expr* callee, std::vector<Expr*> args);
    static Expr* makeFuncDef(int line, std::vector<const char*> params, Stmt* body);

    static void destroy(Expr* e);
};

enum class StmtType : uint8_t {
    BLOCK,
    LOCAL_DECL,
    ASSIGN,
    IF, WHILE, REPEAT,
    CALL,
    RETURN,
};

struct BlockData   { std::vector<Stmt*> stmts; };
struct LocalDeclData { std::vector<const char*> names; std::vector<Expr*> inits; };
struct AssignData  { const char* name; Expr* value; };
struct WhileData   { Expr* cond; Stmt* body; };
struct RepeatData  { Stmt* body; Expr* cond; };
struct CallStmtData { Expr* call; };
struct ReturnData  { Expr* value; };
struct IfData {
    std::vector<Expr*> conds;
    std::vector<Stmt*> bodies;
    Stmt* elseBody;
};

struct Stmt {
    StmtType type;
    int line;
    void* data = nullptr;

    static Stmt* makeBlock(int line, std::vector<Stmt*> stmts);
    static Stmt* makeLocalDecl(int line, std::vector<const char*> names, std::vector<Expr*> inits);
    static Stmt* makeAssign(int line, const char* name, Expr* value);
    static Stmt* makeWhile(int line, Expr* cond, Stmt* body);
    static Stmt* makeRepeat(int line, Stmt* body, Expr* cond);
    static Stmt* makeIf(int line, std::vector<Expr*> conds, std::vector<Stmt*> bodies, Stmt* elseBody);
    static Stmt* makeCallStmt(int line, Expr* call);
    static Stmt* makeReturn(int line, Expr* value);

    static void destroy(Stmt* s);
};
