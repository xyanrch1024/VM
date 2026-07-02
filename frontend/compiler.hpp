#pragma once
#include "ast.hpp"
#include "vm.hpp"
#include "chunk.hpp"
#include <vector>
#include <string>
#include <unordered_map>

struct LocalVar {
    const char* name;
    int depth;
    int slot;
};

struct Upvalue {
    int index;      // index in enclosing function's locals/upvalues
    bool isLocal;   // true if it's a local in enclosing function
    const char* name;
};

class Compiler {
public:
    Compiler(VM& vm);
    Function* compile(const std::vector<Stmt*>& stmts, const char* name);
    bool hadError() const { return hadError_; }

private:
    struct CompileState {
        Function* function;
        CompileState* enclosing = nullptr;
        std::vector<LocalVar> locals;
        std::vector<Upvalue> upvalues;
        int scopeDepth = 0;
        int numLocals = 0;
    };

    VM& vm;
    CompileState* current = nullptr;
    bool hadError_ = false;

    CompileState& curr() { return *current; }

    void error(int line, const char* format, ...);

    int addLocal(const char* name);
    int resolveLocal(const char* name);
    int addUpvalue(int index, bool isLocal);
    int resolveUpvalue(const char* name);

    void pushScope();
    void popScope();

    int emitJump(int line, Opcode op);
    void patchJump(int offset);
    int emitLoop(int line, int loopStart);

    void emitByte(uint8_t byte);
    void emitOpcode(int line, Opcode op);
    void emitConstant(int line, Value v);
    void emitReturn(int line);

    // Compile functions
    void compileStmt(Stmt* stmt);
    void compileBlock(Stmt* stmt);
    void compileExpr(Expr* expr);

    Function* enterFunction(const char* name, int arity);
    Function* leaveFunction();
};
