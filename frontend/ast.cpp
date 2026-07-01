#include "ast.hpp"
#include <cstring>
#include <cstdlib>

Expr* Expr::makeNil(int line) {
    auto e = (Expr*)calloc(1, sizeof(Expr));
    e->type = ExprType::NIL; e->line = line;
    return e;
}

Expr* Expr::makeBool(int line, bool v) {
    auto e = (Expr*)calloc(1, sizeof(Expr));
    e->type = ExprType::BOOL; e->line = line; e->boolVal = v;
    return e;
}

Expr* Expr::makeNum(int line, double v) {
    auto e = (Expr*)calloc(1, sizeof(Expr));
    e->type = ExprType::NUMBER; e->line = line; e->numVal = v;
    return e;
}

Expr* Expr::makeStr(int line, const char* v) {
    auto e = (Expr*)calloc(1, sizeof(Expr));
    e->type = ExprType::STRING; e->line = line; e->strVal = v;
    return e;
}

Expr* Expr::makeName(int line, const char* v) {
    auto e = (Expr*)calloc(1, sizeof(Expr));
    e->type = ExprType::NAME; e->line = line; e->strVal = v;
    return e;
}

Expr* Expr::makeUnary(int line, TokenType op, Expr* rhs) {
    auto e = (Expr*)calloc(1, sizeof(Expr));
    e->type = ExprType::UNARY; e->line = line;
    auto d = new UnaryData;
    d->op = op; d->rhs = rhs;
    e->data = d;
    return e;
}

Expr* Expr::makeBinary(int line, TokenType op, Expr* lhs, Expr* rhs) {
    auto e = (Expr*)calloc(1, sizeof(Expr));
    e->type = ExprType::BINARY; e->line = line;
    auto d = new BinaryData;
    d->op = op; d->lhs = lhs; d->rhs = rhs;
    e->data = d;
    return e;
}

Expr* Expr::makeCall(int line, Expr* callee, std::vector<Expr*> args) {
    auto e = (Expr*)calloc(1, sizeof(Expr));
    e->type = ExprType::CALL; e->line = line;
    auto d = new CallData;
    d->callee = callee; d->args = std::move(args);
    e->data = d;
    return e;
}

Expr* Expr::makeFuncDef(int line, std::vector<const char*> params, Stmt* body) {
    auto e = (Expr*)calloc(1, sizeof(Expr));
    e->type = ExprType::FUNCDEF; e->line = line;
    auto d = new FuncDefData;
    d->params = std::move(params); d->body = body;
    e->data = d;
    return e;
}

void Expr::destroy(Expr* e) {
    if (!e) return;
    switch (e->type) {
        case ExprType::STRING:
        case ExprType::NAME:
            ::free((void*)e->strVal);
            break;
        case ExprType::UNARY: {
            auto d = (UnaryData*)e->data;
            destroy(d->rhs); delete d;
            break;
        }
        case ExprType::BINARY: {
            auto d = (BinaryData*)e->data;
            destroy(d->lhs); destroy(d->rhs); delete d;
            break;
        }
        case ExprType::CALL: {
            auto d = (CallData*)e->data;
            destroy(d->callee);
            for (auto a : d->args) destroy(a);
            delete d;
            break;
        }
        case ExprType::FUNCDEF: {
            auto d = (FuncDefData*)e->data;
            for (auto p : d->params) ::free((void*)p);
            Stmt::destroy(d->body);
            delete d;
            break;
        }
        default: break;
    }
    ::free(e);
}

// === Stmt factory methods ===

Stmt* Stmt::makeBlock(int line, std::vector<Stmt*> stmts) {
    auto s = (Stmt*)calloc(1, sizeof(Stmt));
    s->type = StmtType::BLOCK; s->line = line;
    auto d = new BlockData;
    d->stmts = std::move(stmts);
    s->data = d;
    return s;
}

Stmt* Stmt::makeLocalDecl(int line, std::vector<const char*> names, std::vector<Expr*> inits) {
    auto s = (Stmt*)calloc(1, sizeof(Stmt));
    s->type = StmtType::LOCAL_DECL; s->line = line;
    auto d = new LocalDeclData;
    d->names = std::move(names); d->inits = std::move(inits);
    s->data = d;
    return s;
}

Stmt* Stmt::makeAssign(int line, const char* name, Expr* value) {
    auto s = (Stmt*)calloc(1, sizeof(Stmt));
    s->type = StmtType::ASSIGN; s->line = line;
    auto d = new AssignData;
    d->name = name; d->value = value;
    s->data = d;
    return s;
}

Stmt* Stmt::makeWhile(int line, Expr* cond, Stmt* body) {
    auto s = (Stmt*)calloc(1, sizeof(Stmt));
    s->type = StmtType::WHILE; s->line = line;
    auto d = new WhileData;
    d->cond = cond; d->body = body;
    s->data = d;
    return s;
}

Stmt* Stmt::makeRepeat(int line, Stmt* body, Expr* cond) {
    auto s = (Stmt*)calloc(1, sizeof(Stmt));
    s->type = StmtType::REPEAT; s->line = line;
    auto d = new RepeatData;
    d->body = body; d->cond = cond;
    s->data = d;
    return s;
}

Stmt* Stmt::makeIf(int line, std::vector<Expr*> conds, std::vector<Stmt*> bodies, Stmt* elseBody) {
    auto s = (Stmt*)calloc(1, sizeof(Stmt));
    s->type = StmtType::IF; s->line = line;
    auto d = new IfData;
    d->conds = std::move(conds); d->bodies = std::move(bodies); d->elseBody = elseBody;
    s->data = d;
    return s;
}

Stmt* Stmt::makeCallStmt(int line, Expr* call) {
    auto s = (Stmt*)calloc(1, sizeof(Stmt));
    s->type = StmtType::CALL; s->line = line;
    auto d = new CallStmtData;
    d->call = call;
    s->data = d;
    return s;
}

Stmt* Stmt::makeReturn(int line, Expr* value) {
    auto s = (Stmt*)calloc(1, sizeof(Stmt));
    s->type = StmtType::RETURN; s->line = line;
    auto d = new ReturnData;
    d->value = value;
    s->data = d;
    return s;
}

void Stmt::destroy(Stmt* s) {
    if (!s) return;
    if (s->data) {
        switch (s->type) {
            case StmtType::BLOCK: {
                auto d = (BlockData*)s->data;
                for (auto st : d->stmts) destroy(st);
                delete d; break;
            }
            case StmtType::LOCAL_DECL: {
                auto d = (LocalDeclData*)s->data;
                for (auto n : d->names) ::free((void*)n);
                for (auto i : d->inits) Expr::destroy(i);
                delete d; break;
            }
            case StmtType::ASSIGN: {
                auto d = (AssignData*)s->data;
                ::free((void*)d->name);
                Expr::destroy(d->value);
                delete d; break;
            }
            case StmtType::WHILE: {
                auto d = (WhileData*)s->data;
                Expr::destroy(d->cond); destroy(d->body);
                delete d; break;
            }
            case StmtType::REPEAT: {
                auto d = (RepeatData*)s->data;
                destroy(d->body); Expr::destroy(d->cond);
                delete d; break;
            }
            case StmtType::IF: {
                auto d = (IfData*)s->data;
                for (auto c : d->conds) Expr::destroy(c);
                for (auto b : d->bodies) destroy(b);
                destroy(d->elseBody);
                delete d; break;
            }
            case StmtType::CALL: {
                auto d = (CallStmtData*)s->data;
                Expr::destroy(d->call);
                delete d; break;
            }
            case StmtType::RETURN: {
                auto d = (ReturnData*)s->data;
                Expr::destroy(d->value);
                delete d; break;
            }
        }
    }
    ::free(s);
}
