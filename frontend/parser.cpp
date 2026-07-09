#include "parser.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>

static char* copyStr(std::string_view sv) {
    char* s = (char*)malloc(sv.size() + 1);
    memcpy(s, sv.data(), sv.size());
    s[sv.size()] = '\0';
    return s;
}

Parser::Parser(const std::string& source) : lexer(source) {
    advance(); // load first token
}

void Parser::advance() {
    previous = current;
    for (;;) {
        current = lexer.next();
        if (current.type != TokenType::TK_EOF) break;
        // Actually skipToNextLine for error recovery is handled in synchronize
        break;
    }
    if (current.type == TokenType::TK_EOF && previous.type == TokenType::TK_EOF) {
        // end
    }
}

void Parser::consume(TokenType type, const char* msg) {
    if (current.type == type) { advance(); return; }
    if (!panicMode) {
        fprintf(stderr, "[line %d] error: %s\n", current.line, msg);
        hadError = true;
    }
    panicMode = true;
    throw ParseError();
}

bool Parser::check(TokenType type) {
    return current.type == type;
}

bool Parser::match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

void Parser::synchronize() {
    panicMode = false;
    while (current.type != TokenType::TK_EOF) {
        if (previous.type == TokenType::TK_SEMI) return;
        switch (current.type) {
            case TokenType::TK_LOCAL:
            case TokenType::TK_IF:
            case TokenType::TK_WHILE:
            case TokenType::TK_REPEAT:
            case TokenType::TK_FOR:
            case TokenType::TK_FUNCTION:
            case TokenType::TK_RETURN:
            case TokenType::TK_END:
            case TokenType::TK_ELSE:
            case TokenType::TK_ELSEIF:
            case TokenType::TK_UNTIL:
                return;
            default:
                advance();
        }
    }
}

std::vector<Stmt*> Parser::parse() {
    hadError = false;
    try {
        return program();
    } catch (ParseError&) {
        if (panicMode) synchronize();
        return {};
    }
}

std::vector<Stmt*> Parser::parseQuiet() {
    // Suppress error output by redirecting stderr
    FILE* oldStderr = stderr;
    FILE* devnull = fopen("/dev/null", "w");
    if (devnull) stderr = devnull;

    auto result = parse();

    if (devnull) {
        stderr = oldStderr;
        fclose(devnull);
    }
    return result;
}

// ---- Grammar ----

std::vector<Stmt*> Parser::program() {
    std::vector<Stmt*> stmts;
    while (!check(TokenType::TK_EOF)) {
        try {
            stmts.push_back(statement());
        } catch (ParseError&) {
            if (panicMode) synchronize();
        }
    }
    return stmts;
}

Stmt* Parser::statement() {
    if (match(TokenType::TK_LOCAL)) return localDecl();
    if (match(TokenType::TK_IF))    return ifStmt();
    if (match(TokenType::TK_WHILE)) return whileStmt();
    if (match(TokenType::TK_REPEAT)) return repeatStmt();
    if (match(TokenType::TK_RETURN)) return returnStmt();
    if (match(TokenType::TK_SEMI))  return nullptr;

    // function name(params) body end  →  name = function(params) body end
    if (match(TokenType::TK_FUNCTION)) {
        if (current.type == TokenType::TK_NAME) {
            const char* name = copyStr(current.start);
            int line = current.line;
            advance();
            Expr* func = funcDef();
            return Stmt::makeAssign(line, name, func);
        }
        fprintf(stderr, "[line %d] error: expected function name\n", current.line);
        hadError = true;
        return nullptr;
    }

    // Expression or assignment
    Expr* e = expression();

    if (current.type == TokenType::TK_ASSIGN) {
        int line = e ? e->line : 0;
        if (e && e->type == ExprType::NAME) {
            const char* name = strdup(e->strVal);
            Expr::destroy(e);
            advance();
            Expr* val = expression();
            return Stmt::makeAssign(line, name, val);
        } else if (e && e->type == ExprType::INDEX) {
            auto* idx = (IndexData*)e->data;
            Expr* obj = idx->obj;
            Expr* key = idx->key;
            idx->obj = idx->key = nullptr;
            Expr::destroy(e);
            advance();
            Expr* val = expression();
            return Stmt::makeIndexAssign(line, obj, key, val);
        } else {
            fprintf(stderr, "[line %d] error: invalid assignment target\n", line);
            hadError = true;
            Expr::destroy(e);
            advance();
            Expr::destroy(expression());
            return nullptr;
        }
    }

    if (e && e->type == ExprType::CALL) {
        return Stmt::makeCallStmt(e->line, e);
    }

    if (e) {
        fprintf(stderr, "[line %d] error: expression has no effect\n", e->line);
        hadError = true;
        Expr::destroy(e);
    }
    return nullptr;
}

Stmt* Parser::localDecl() {
    int line = previous.line;

    // local function name(params) body end
    // desugar: local name = function(params) body end
    if (match(TokenType::TK_FUNCTION)) {
        consume(TokenType::TK_NAME, "expected function name");
        const char* name = copyStr(previous.start);
        int funcLine = previous.line;
        Expr* func = funcDef();
        std::vector<const char*> names = {name};
        std::vector<Expr*> inits = {func};
        return Stmt::makeLocalDecl(funcLine, names, inits);
    }

    std::vector<const char*> names;
    if (current.type != TokenType::TK_NAME) {
        consume(TokenType::TK_NAME, "expected variable name");
        return nullptr;
    }
    names.clear();
    do {
        advance(); // consume name
    names.push_back(copyStr(previous.start));
    } while (match(TokenType::TK_COMMA));

    std::vector<Expr*> inits;
    if (match(TokenType::TK_ASSIGN)) {
        do {
            inits.push_back(expression());
        } while (match(TokenType::TK_COMMA));
    }
    // Resize inits to match names (pad with nil)
    while ((int)inits.size() < (int)names.size()) {
        inits.push_back(Expr::makeNil(line));
    }
    return Stmt::makeLocalDecl(line, names, inits);
}

Stmt* Parser::ifStmt() {
    int line = previous.line;
    std::vector<Expr*> conds;
    std::vector<Stmt*> bodies;

    // First if condition
    conds.push_back(expression());
    consume(TokenType::TK_THEN, "expected 'then'");
    bodies.push_back(block());

    // elseif clauses
    while (match(TokenType::TK_ELSEIF)) {
        conds.push_back(expression());
        consume(TokenType::TK_THEN, "expected 'then'");
        bodies.push_back(block());
    }

    // else clause
    Stmt* elseBody = nullptr;
    if (match(TokenType::TK_ELSE)) {
        elseBody = block();
    }

    consume(TokenType::TK_END, "expected 'end'");
    return Stmt::makeIf(line, conds, bodies, elseBody);
}

Stmt* Parser::whileStmt() {
    int line = previous.line;
    Expr* cond = expression();
    consume(TokenType::TK_DO, "expected 'do'");
    Stmt* body = block();
    consume(TokenType::TK_END, "expected 'end'");
    return Stmt::makeWhile(line, cond, body);
}

Stmt* Parser::repeatStmt() {
    int line = previous.line;
    Stmt* body = block();
    consume(TokenType::TK_UNTIL, "expected 'until'");
    Expr* cond = expression();
    return Stmt::makeRepeat(line, body, cond);
}

Stmt* Parser::returnStmt() {
    int line = previous.line;
    Expr* value = nullptr;
    if (current.type != TokenType::TK_END &&
        current.type != TokenType::TK_ELSE &&
        current.type != TokenType::TK_ELSEIF &&
        current.type != TokenType::TK_UNTIL &&
        current.type != TokenType::TK_EOF) {
        value = expression();
    }
    return Stmt::makeReturn(line, value);
}

Stmt* Parser::block() {
    return Stmt::makeBlock(previous.line, blockStmts());
}

std::vector<Stmt*> Parser::blockStmts() {
    std::vector<Stmt*> stmts;
    while (!check(TokenType::TK_EOF) &&
           !check(TokenType::TK_END) &&
           !check(TokenType::TK_ELSE) &&
           !check(TokenType::TK_ELSEIF) &&
           !check(TokenType::TK_UNTIL)) {
        Stmt* s = statement();
        if (s) stmts.push_back(s);
    }
    return stmts;
}

// ---- Expressions ----

Expr* Parser::expression() { return orExpr(); }

Expr* Parser::orExpr() {
    Expr* e = andExpr();
    while (match(TokenType::TK_OR)) {
        TokenType op = previous.type;
        Expr* rhs = andExpr();
        e = Expr::makeBinary(previous.line, op, e, rhs);
    }
    return e;
}

Expr* Parser::andExpr() {
    Expr* e = cmpExpr();
    while (match(TokenType::TK_AND)) {
        TokenType op = previous.type;
        Expr* rhs = cmpExpr();
        e = Expr::makeBinary(previous.line, op, e, rhs);
    }
    return e;
}

Expr* Parser::cmpExpr() {
    Expr* e = concatExpr();
    while (match(TokenType::TK_EQ) || match(TokenType::TK_NE) ||
           match(TokenType::TK_LT) || match(TokenType::TK_GT) ||
           match(TokenType::TK_LE) || match(TokenType::TK_GE)) {
        TokenType op = previous.type;
        Expr* rhs = concatExpr();
        e = Expr::makeBinary(previous.line, op, e, rhs);
    }
    return e;
}

Expr* Parser::concatExpr() {
    Expr* e = addExpr();
    while (match(TokenType::TK_DOTDOT)) {
        TokenType op = previous.type;
        Expr* rhs = addExpr();
        e = Expr::makeBinary(previous.line, op, e, rhs);
    }
    return e;
}

Expr* Parser::addExpr() {
    Expr* e = mulExpr();
    while (match(TokenType::TK_PLUS) || match(TokenType::TK_MINUS)) {
        TokenType op = previous.type;
        Expr* rhs = mulExpr();
        e = Expr::makeBinary(previous.line, op, e, rhs);
    }
    return e;
}

Expr* Parser::mulExpr() {
    Expr* e = powExpr();
    while (match(TokenType::TK_STAR) || match(TokenType::TK_SLASH) || match(TokenType::TK_PERCENT)) {
        TokenType op = previous.type;
        Expr* rhs = powExpr();
        e = Expr::makeBinary(previous.line, op, e, rhs);
    }
    return e;
}

Expr* Parser::powExpr() {
    Expr* e = unaryExpr();
    if (match(TokenType::TK_CARET)) {
        TokenType op = previous.type;
        Expr* rhs = powExpr(); // right-associative
        e = Expr::makeBinary(previous.line, op, e, rhs);
    }
    return e;
}

Expr* Parser::unaryExpr() {
    if (match(TokenType::TK_MINUS) || match(TokenType::TK_NOT)) {
        TokenType op = previous.type;
        Expr* rhs = unaryExpr();
        return Expr::makeUnary(previous.line, op, rhs);
    }
    return postfixExpr();
}

Expr* Parser::postfixExpr() {
    Expr* e = prefixExpr();
    for (;;) {
        if (match(TokenType::TK_LPAREN)) {
            std::vector<Expr*> args;
            if (!check(TokenType::TK_RPAREN)) {
                do {
                    args.push_back(expression());
                } while (match(TokenType::TK_COMMA));
            }
            consume(TokenType::TK_RPAREN, "expected ')'");
            e = Expr::makeCall(previous.line, e, args);
        } else if (match(TokenType::TK_LBRACK)) {
            Expr* key = expression();
            consume(TokenType::TK_RBRACK, "expected ']'");
            e = Expr::makeIndex(previous.line, e, key);
        } else if (match(TokenType::TK_DOT)) {
            consume(TokenType::TK_NAME, "expected name after '.'");
            Expr* key = Expr::makeStr(previous.line, copyStr(previous.start));
            e = Expr::makeIndex(previous.line, e, key);
        } else {
            break;
        }
    }
    return e;
}

Expr* Parser::prefixExpr() {
    if (match(TokenType::TK_NAME)) {
        return Expr::makeName(previous.line, copyStr(previous.start));
    }
    if (match(TokenType::TK_LPAREN)) {
        Expr* e = expression();
        consume(TokenType::TK_RPAREN, "expected ')'");
        return e;
    }
    // Table constructor: { fieldlist }
    if (match(TokenType::TK_LBRACE)) {
        return tableConstructor();
    }
    return primaryExpr();
}

Expr* Parser::primaryExpr() {
    if (match(TokenType::TK_NIL))    return Expr::makeNil(previous.line);
    if (match(TokenType::TK_TRUE))   return Expr::makeBool(previous.line, true);
    if (match(TokenType::TK_FALSE))  return Expr::makeBool(previous.line, false);
    if (match(TokenType::TK_NUMBER)) {
        char* end = nullptr;
        double v = strtod(std::string(previous.start).c_str(), &end);
        return Expr::makeNum(previous.line, v);
    }
    if (match(TokenType::TK_STRING)) {
        return Expr::makeStr(previous.line, copyStr(previous.start));
    }
    if (match(TokenType::TK_FUNCTION)) {
        return funcDef();
    }

    fprintf(stderr, "[line %d] error: expected expression\n", current.line);
    hadError = true;
    throw ParseError();
}

Expr* Parser::tableConstructor() {
    int line = previous.line;
    std::vector<TableField> fields;

    if (!check(TokenType::TK_RBRACE)) {
        do {
            if (check(TokenType::TK_RBRACE)) break;
            TableField field = {nullptr, nullptr};

            if (match(TokenType::TK_LBRACK)) {
                field.key = expression();
                consume(TokenType::TK_RBRACK, "expected ']'");
                consume(TokenType::TK_ASSIGN, "expected '='");
                field.value = expression();
            } else if (check(TokenType::TK_NAME)) {
                const char* name = copyStr(current.start);
                int nameLine = current.line;
                advance();
                if (match(TokenType::TK_ASSIGN)) {
                    field.key = Expr::makeStr(nameLine, name);
                    field.value = expression();
                } else {
                    field.value = Expr::makeName(nameLine, name);
                }
            } else {
                field.value = expression();
            }

            fields.push_back(field);
        } while (match(TokenType::TK_COMMA));
    }
    consume(TokenType::TK_RBRACE, "expected '}'");

    int implicitKey = 1;
    for (auto& f : fields) {
        if (f.key == nullptr) {
            f.key = Expr::makeNum(line, implicitKey);
            implicitKey++;
        }
    }

    auto* arr = new TableField[fields.size()];
    memcpy(arr, fields.data(), fields.size() * sizeof(TableField));
    return Expr::makeTable(line, arr, (int)fields.size());
}

Expr* Parser::funcDef() {
    int line = previous.line;
    consume(TokenType::TK_LPAREN, "expected '('");
    std::vector<const char*> params;
    if (!check(TokenType::TK_RPAREN)) {
        do {
            consume(TokenType::TK_NAME, "expected parameter name");
            params.push_back(copyStr(previous.start));
        } while (match(TokenType::TK_COMMA));
    }
    consume(TokenType::TK_RPAREN, "expected ')'");
    Stmt* body = block();
    consume(TokenType::TK_END, "expected 'end'");
    return Expr::makeFuncDef(line, params, body);
}
