#pragma once
#include "chunk.hpp"
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>

struct CallFrame {
    Function* function = nullptr;
    int pc = 0;
    int fp = 0; // stack index of frame base
};

class VM {
public:
    enum Result { OK, RUNTIME_ERROR };

    VM();
    ~VM();

    // Execute a function (or create one from chunk)
    Result interpret(Function* func);

    // Ownership helpers
    Function* newFunction(const std::string& name, int arity = 0);
    void addFunction(Function* func);
    Function* getFunction(int index);

    // String interning
    std::string* internString(const std::string& s);
    std::string* concatStrings(const std::string& a, const std::string& b);

    // Debug
    void dumpStack() const;

private:
    // Stack
    std::vector<Value> stack;

    // Call frames
    std::vector<CallFrame> frames;

    // Owned functions
    std::vector<std::unique_ptr<Function>> functions;

    // String table (stable pointers)
    std::vector<std::unique_ptr<std::string>> stringTable;
    std::unordered_map<std::string_view, std::string*> stringMap;

    // Current execution context
    CallFrame* frame() { return &frames.back(); }
    Chunk* chunk() { return frame()->function->chunk.code.data() ? &frame()->function->chunk : nullptr; }

    uint8_t readByte() { return frame()->function->chunk.code[frame()->pc++]; }
    uint16_t readShort() {
        uint16_t b1 = readByte();
        uint16_t b2 = readByte();
        return b1 | (b2 << 8);
    }
    Value readConstant() {
        uint8_t instr = frame()->function->chunk.code[frame()->pc - 1];
        if (instr == OP_CONSTANT) {
            uint8_t idx = readByte();
            return frame()->function->chunk.constants[idx];
        } else {
            uint16_t idx = readShort();
            return frame()->function->chunk.constants[idx];
        }
    }

    void push(Value v);
    Value pop();
    Value peek(int distance = 0);
    void resetStack();

    void runtimeError(const char* format, ...);
};
