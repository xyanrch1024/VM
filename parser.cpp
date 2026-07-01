#include "parser.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>

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
    // Expression statement (function call or assignment)
    Expr* e = expression();
    if (e && e->type == ExprType::CALL) {
        return Stmt::makeCallStmt(e->line, e);
    }
    // Assignment
    if (e && e->type == ExprType::NAME && current.type == TokenType::TK_ASSIGN) {
        const char* name = e->strVal;
        Expr::destroy(e);
        advance(); // consume =
        Expr* val = expression();
        return Stmt::makeAssign(name ? previous.line : 0, name, val);
    }
    // Function definition: function name(params) body end
    // Handled differently - it's a statement
    // Actually 'function' keyword is handled above via match, but we need to handle
    // function name(...) as a statement. Let me add a case.
    // Actually, it's handled as prefixExpr → funcDef. But 'function' keyword at statement
    // level with a name after it is a statement:
    //   function f(x) body end
    // This is syntactic sugar for:
    //   f = function(x) body end
    // Let me handle it here:
    if (check(TokenType::TK_FUNCTION)) {
        // Actually we should parse function name(...) body end
        // This requires lookahead. Let me handle this in a special way.
        // For now, consume and report.
        fprintf(stderr, "[line %d] error: function statement not handled yet\n", current.line);
        hadError = true;
        return nullptr;
    }
    // Otherwise it's an expression statement
    if (e) {
        fprintf(stderr, "[line %d] error: expression has no effect\n", e->line);
        hadError = true;
        Expr::destroy(e);
    }
    return nullptr;
}

Stmt* Parser::localDecl() {
    int line = previous.line;
    std::vector<const char*> names;
    names.push_back(previous.start.data());
    // Actually, match() already advanced past 'local'. Now we need names.
    // But wait, 'local' was already consumed by statement(). Let me re-read...
    // match(TK_LOCAL) returns true and advances. So previous is 'local'.
    // Now we need to read the variable name(s)
    if (current.type != TokenType::TK_NAME) {
        consume(TokenType::TK_NAME, "expected variable name");
        return nullptr;
    }
    names.clear();
    do {
        advance(); // consume name
        names.push_back(previous.start.data());
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
        // TK_LEN is '#'
        TokenType op = previous.type;
        Expr* rhs = unaryExpr();
        return Expr::makeUnary(previous.line, op, rhs);
    }
    return callExpr();
}

Expr* Parser::callExpr() {
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
        } else {
            break;
        }
    }
    return e;
}

Expr* Parser::prefixExpr() {
    if (match(TokenType::TK_NAME)) {
        return Expr::makeName(previous.line, previous.start.data());
    }
    if (match(TokenType::TK_LPAREN)) {
        Expr* e = expression();
        consume(TokenType::TK_RPAREN, "expected ')'");
        return e;
    }
    // Function definition as expression: function(params) body end
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
        return Expr::makeStr(previous.line, std::string(previous.start).c_str());
    }
    if (match(TokenType::TK_FUNCTION)) {
        return funcDef();
    }

    fprintf(stderr, "[line %d] error: expected expression\n", current.line);
    hadError = true;
    throw ParseError();
}

Expr* Parser::funcDef() {
    int line = previous.line;
    consume(TokenType::TK_LPAREN, "expected '('");
    std::vector<const char*> params;
    if (!check(TokenType::TK_RPAREN)) {
        do {
            consume(TokenType::TK_NAME, "expected parameter name");
            params.push_back(previous.start.data());
        } while (match(TokenType::TK_COMMA));
    }
    consume(TokenType::TK_RPAREN, "expected ')'");
    Stmt* body = block();
    consume(TokenType::TK_END, "expected 'end'");
    return Expr::makeFuncDef(line, params, body);
}
