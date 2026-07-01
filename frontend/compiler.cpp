#include "compiler.hpp"
#include <cstdio>
#include <cstdarg>
#include <cstring>

Compiler::Compiler(VM& v) : vm(v) {}

static bool isPrintCall(CallData* cd) {
    return cd->callee->type == ExprType::NAME &&
           strcmp(cd->callee->strVal, "print") == 0;
}

void Compiler::error(int line, const char* format, ...) {
    hadError_ = true;
    va_list args;
    va_start(args, format);
    fprintf(stderr, "[line %d] error: ", line);
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}

// ---- Bytecode Emission ----

void Compiler::emitByte(uint8_t byte) {
    curr().function->chunk.write(byte, curr().function->chunk.lines.empty() ? 0 : curr().function->chunk.lines.back());
}

void Compiler::emitOpcode(int line, Opcode op) {
    if (line > 0)
        curr().function->chunk.writeOpcode(op, line);
    else
        curr().function->chunk.write(static_cast<uint8_t>(op),
            curr().function->chunk.lines.empty() ? 0 : curr().function->chunk.lines.back());
}

void Compiler::emitConstant(int line, Value v) {
    int idx = curr().function->chunk.addConstant(v);
    if (idx < 256) {
        emitOpcode(line, OP_CONSTANT);
        emitByte((uint8_t)idx);
    } else {
        emitOpcode(line, OP_CONSTANT_LONG);
        emitByte((uint8_t)(idx & 0xFF));
        emitByte((uint8_t)((idx >> 8) & 0xFF));
    }
}

void Compiler::emitReturn(int line) {
    emitOpcode(line, OP_NIL);
    emitOpcode(line, OP_RET);
}

int Compiler::emitJump(int line, Opcode op) {
    emitOpcode(line, op);
    emitByte(0xFF);
    emitByte(0xFF);
    return (int)curr().function->chunk.code.size() - 2;
}

void Compiler::patchJump(int offset) {
    int16_t jump = (int16_t)((int)curr().function->chunk.code.size() - offset - 2);
    curr().function->chunk.code[offset]     = (uint8_t)(jump & 0xFF);
    curr().function->chunk.code[offset + 1] = (uint8_t)((jump >> 8) & 0xFF);
}

int Compiler::emitLoop(int line, int loopStart) {
    emitOpcode(line, OP_LOOP);
    int offset = (int)curr().function->chunk.code.size();
    int16_t jump = (int16_t)((int)curr().function->chunk.code.size() + 2 - loopStart);
    emitByte((uint8_t)(jump & 0xFF));
    emitByte((uint8_t)((jump >> 8) & 0xFF));
    return offset;
}

// ---- Variable Management ----

int Compiler::addLocal(const char* name) {
    LocalVar lv;
    lv.name = name;
    lv.depth = curr().scopeDepth;
    lv.slot = curr().numLocals++;
    curr().function->numLocals = curr().numLocals;
    curr().locals.push_back(lv);
    return lv.slot;
}

int Compiler::resolveLocal(const char* name) {
    for (int i = (int)curr().locals.size() - 1; i >= 0; i--) {
        if (curr().locals[i].name && strcmp(curr().locals[i].name, name) == 0) {
            return curr().locals[i].slot;
        }
    }
    return -1;
}

int Compiler::addUpvalue(int index, bool isLocal) {
    for (int i = 0; i < (int)curr().upvalues.size(); i++) {
        if (curr().upvalues[i].index == index && curr().upvalues[i].isLocal == isLocal) {
            return i;
        }
    }
    Upvalue uv;
    uv.index = index;
    uv.isLocal = isLocal;
    curr().upvalues.push_back(uv);
    return (int)curr().upvalues.size() - 1;
}

int Compiler::resolveUpvalue(const char* name) {
    if (stateStack.size() < 2) return -1;
    auto& enc = stateStack[stateStack.size() - 2];
    for (int i = (int)enc.locals.size() - 1; i >= 0; i--) {
        if (enc.locals[i].name && strcmp(enc.locals[i].name, name) == 0) {
            return addUpvalue(i, true);
        }
    }
    return -1;
}

void Compiler::pushScope() { curr().scopeDepth++; }

void Compiler::popScope() {
    curr().scopeDepth--;
    while (!curr().locals.empty() && curr().locals.back().depth > curr().scopeDepth) {
        emitOpcode(0, OP_POP);
        curr().locals.pop_back();
    }
}

// ---- Function Enter/Leave ----

Function* Compiler::enterFunction(const char* name, int arity) {
    Function* func = vm.newFunction(name, arity);
    CompileState state;
    state.function = func;
    state.scopeDepth = 0;
    state.numLocals = 0;
    stateStack.push_back(state);
    return func;
}

Function* Compiler::leaveFunction() {
    emitReturn(0);
    Function* func = curr().function;
    func->numLocals = curr().numLocals;
    stateStack.pop_back();
    return func;
}

// ---- Compile Statements ----

void Compiler::compileStmt(Stmt* stmt) {
    if (!stmt) return;
    switch (stmt->type) {
        case StmtType::BLOCK: {
            auto d = (BlockData*)stmt->data;
            pushScope();
            for (auto s : d->stmts) compileStmt(s);
            popScope();
            break;
        }

        case StmtType::LOCAL_DECL: {
            auto d = (LocalDeclData*)stmt->data;
            for (size_t i = 0; i < d->inits.size(); i++) {
                compileExpr(d->inits[i]);
            }
            for (size_t i = 0; i < d->names.size(); i++) {
                if (i >= d->inits.size()) emitOpcode(stmt->line, OP_NIL);
                int slot = addLocal(d->names[i]);
                if (slot < 4) {
                    emitOpcode(stmt->line, (Opcode)(OP_STORE_0 + slot));
                } else {
                    emitOpcode(stmt->line, OP_STORE);
                    emitByte((uint8_t)slot);
                }
                emitOpcode(stmt->line, OP_POP);
            }
            break;
        }

        case StmtType::ASSIGN: {
            auto d = (AssignData*)stmt->data;
            compileExpr(d->value);
            int slot = resolveLocal(d->name);
            if (slot < 0) { error(stmt->line, "undefined variable '%s'", d->name); return; }
            if (slot < 4) {
                emitOpcode(stmt->line, (Opcode)(OP_STORE_0 + slot));
            } else {
                emitOpcode(stmt->line, OP_STORE);
                emitByte((uint8_t)slot);
            }
            emitOpcode(stmt->line, OP_POP);
            break;
        }

        case StmtType::IF: {
            auto d = (IfData*)stmt->data;
            std::vector<int> endJumps;
            for (size_t i = 0; i < d->conds.size(); i++) {
                compileExpr(d->conds[i]);
                int jElse = emitJump(stmt->line, OP_JZ);
                compileBlock(d->bodies[i]);
                if (i < d->conds.size() - 1 || d->elseBody) {
                    endJumps.push_back(emitJump(stmt->line, OP_JMP));
                }
                patchJump(jElse);
            }
            if (d->elseBody) compileBlock(d->elseBody);
            for (int j : endJumps) patchJump(j);
            break;
        }

        case StmtType::WHILE: {
            auto d = (WhileData*)stmt->data;
            int loopStart = (int)curr().function->chunk.code.size();
            compileExpr(d->cond);
            int jExit = emitJump(stmt->line, OP_JZ);
            compileBlock(d->body);
            emitLoop(stmt->line, loopStart);
            patchJump(jExit);
            break;
        }

        case StmtType::REPEAT: {
            auto d = (RepeatData*)stmt->data;
            int loopStart = (int)curr().function->chunk.code.size();
            compileBlock(d->body);
            compileExpr(d->cond);
            // if cond is true, jump to exit (JNZ = jump if true)
            int jExit = emitJump(stmt->line, OP_JNZ);
            // else loop back
            emitLoop(stmt->line, loopStart);
            patchJump(jExit);
            break;
        }

        case StmtType::INDEX_ASSIGN: {
            auto d = (IndexAssignData*)stmt->data;
            compileExpr(d->obj);
            compileExpr(d->key);
            compileExpr(d->value);
            emitOpcode(stmt->line, OP_TABLE_SET);
            break;
        }

        case StmtType::CALL: {
            auto d = (CallStmtData*)stmt->data;
            compileExpr(d->call);
            // print() uses OP_PRINTLN which already pops; other calls
            // need OP_POP to discard the return value on the stack.
            if (d->call->type != ExprType::CALL ||
                !isPrintCall((CallData*)d->call->data))
                emitOpcode(stmt->line, OP_POP);
            break;
        }

        case StmtType::RETURN: {
            auto d = (ReturnData*)stmt->data;
            if (d->value) {
                compileExpr(d->value);
            } else {
                emitOpcode(stmt->line, OP_NIL);
            }
            emitOpcode(stmt->line, OP_RET);
            break;
        }
    }
}

void Compiler::compileBlock(Stmt* stmt) {
    if (stmt->type != StmtType::BLOCK) { compileStmt(stmt); return; }
    auto d = (BlockData*)stmt->data;
    pushScope();
    for (auto s : d->stmts) compileStmt(s);
    popScope();
}

// ---- Compile Expressions ----

void Compiler::compileExpr(Expr* expr) {
    if (!expr) return;
    switch (expr->type) {
        case ExprType::NIL:
            emitOpcode(expr->line, OP_NIL);
            break;

        case ExprType::BOOL:
            emitOpcode(expr->line, expr->boolVal ? OP_TRUE : OP_FALSE);
            break;

        case ExprType::NUMBER: {
            double v = expr->numVal;
            if (v == (double)(int64_t)v) {
                emitConstant(expr->line, Value::makeInt((int64_t)v));
            } else {
                emitConstant(expr->line, Value::makeFloat(v));
            }
            break;
        }

        case ExprType::STRING:
            emitConstant(expr->line, Value::makeStr(new std::string(expr->strVal)));
            break;

        case ExprType::NAME: {
            const char* name = expr->strVal;
            int slot = resolveLocal(name);
            if (slot >= 0) {
                if (slot < 4)
                    emitOpcode(expr->line, (Opcode)(OP_LOAD_0 + slot));
                else {
                    emitOpcode(expr->line, OP_LOAD);
                    emitByte((uint8_t)slot);
                }
            } else {
                error(expr->line, "undefined variable '%s'", name);
            }
            break;
        }

        case ExprType::UNARY: {
            auto d = (UnaryData*)expr->data;
            compileExpr(d->rhs);
            switch (d->op) {
                case TokenType::TK_MINUS: emitOpcode(expr->line, OP_NEG); break;
                case TokenType::TK_NOT:   emitOpcode(expr->line, OP_NOT); break;
                default: error(expr->line, "unknown unary operator"); break;
            }
            break;
        }

        case ExprType::BINARY: {
            auto d = (BinaryData*)expr->data;
            TokenType op = d->op;
            if (op == TokenType::TK_AND || op == TokenType::TK_OR) {
                compileExpr(d->lhs);
                emitOpcode(expr->line, OP_DUP);
                Opcode jumpOp = (op == TokenType::TK_AND) ? OP_JZ : OP_JNZ;
                int j = emitJump(expr->line, jumpOp);
                emitOpcode(expr->line, OP_POP);
                compileExpr(d->rhs);
                patchJump(j);
            } else {
                compileExpr(d->lhs);
                compileExpr(d->rhs);
                switch (op) {
                    case TokenType::TK_PLUS:   emitOpcode(expr->line, OP_ADD); break;
                    case TokenType::TK_MINUS:  emitOpcode(expr->line, OP_SUB); break;
                    case TokenType::TK_STAR:   emitOpcode(expr->line, OP_MUL); break;
                    case TokenType::TK_SLASH:  emitOpcode(expr->line, OP_DIV); break;
                    case TokenType::TK_PERCENT: emitOpcode(expr->line, OP_MOD); break;
                    case TokenType::TK_CARET:  emitOpcode(expr->line, OP_POW); break;
                    case TokenType::TK_DOTDOT: emitOpcode(expr->line, OP_ADD); break;
                    case TokenType::TK_EQ:     emitOpcode(expr->line, OP_EQ); break;
                    case TokenType::TK_NE:     emitOpcode(expr->line, OP_NE); break;
                    case TokenType::TK_LT:     emitOpcode(expr->line, OP_LT); break;
                    case TokenType::TK_GT:     emitOpcode(expr->line, OP_GT); break;
                    case TokenType::TK_LE:     emitOpcode(expr->line, OP_LE); break;
                    case TokenType::TK_GE:     emitOpcode(expr->line, OP_GE); break;
                    default: error(expr->line, "unknown binary operator"); break;
                }
            }
            break;
        }

        case ExprType::CALL: {
            auto d = (CallData*)expr->data;
            for (auto arg : d->args) compileExpr(arg);

            if (d->callee->type == ExprType::NAME) {
                const char* calleeName = d->callee->strVal;
                if (strcmp(calleeName, "print") == 0) {
                    emitOpcode(expr->line, OP_PRINTLN);
                    break;
                }
                if (strcmp(calleeName, "type") == 0) {
                    if (d->args.size() != 1) {
                        error(expr->line, "type() requires exactly one argument");
                        break;
                    }
                    compileExpr(d->args[0]);
                    emitOpcode(expr->line, OP_TYPE);
                    break;
                }

                int slot = resolveLocal(calleeName);
                if (slot < 0) {
                    error(expr->line, "undefined function '%s'", calleeName);
                    break;
                }
                if (slot < 4)
                    emitOpcode(expr->line, (Opcode)(OP_LOAD_0 + slot));
                else {
                    emitOpcode(expr->line, OP_LOAD);
                    emitByte((uint8_t)slot);
                }
                emitOpcode(expr->line, OP_CALL);
                emitByte((uint8_t)d->args.size());
            } else {
                error(expr->line, "calls only supported for named functions");
            }
            break;
        }

        case ExprType::TABLE: {
            auto d = (TableData*)expr->data;
            emitOpcode(expr->line, OP_NEW_TABLE);
            for (int i = 0; i < d->count; i++) {
                emitOpcode(expr->line, OP_DUP);
                compileExpr(d->fields[i].key);
                compileExpr(d->fields[i].value);
                emitOpcode(expr->line, OP_TABLE_SET);
            }
            break;
        }

        case ExprType::INDEX: {
            auto d = (IndexData*)expr->data;
            compileExpr(d->obj);
            compileExpr(d->key);
            emitOpcode(expr->line, OP_TABLE_GET);
            break;
        }

        case ExprType::FUNCDEF: {
            auto d = (FuncDefData*)expr->data;
            Function* func = enterFunction("anonymous", (int)d->params.size());
            for (auto p : d->params) addLocal(p);
            if (d->body) compileBlock(d->body);
            leaveFunction();
            int funcIdx = -1;
            for (int i = 0; i < vm.functionCount(); i++) {
                if (vm.getFunction(i) == func) { funcIdx = i; break; }
            }
            emitConstant(expr->line, Value::makeInt(funcIdx));
            break;
        }
    }
}

// ---- Top Level ----

Function* Compiler::compile(const std::vector<Stmt*>& stmts, const char* name) {
    hadError_ = false;
    stateStack.clear();

    CompileState state;
    state.function = vm.newFunction(name, 0);
    state.scopeDepth = 0;
    state.numLocals = 0;
    stateStack.push_back(state);

    // Register "print" as a built-in local (slot 0)
    addLocal("print");

    for (auto s : stmts) {
        compileStmt(s);
    }

    emitOpcode(0, OP_HALT);
    curr().function->numLocals = curr().numLocals;
    stateStack.pop_back();
    return state.function;
}
