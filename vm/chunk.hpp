#pragma once
#include <cstdint>
#include <cstdio>
#include <vector>
#include <string>
#include "value.hpp"

enum Opcode : uint8_t {
    // Constants
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_CONSTANT,
    OP_CONSTANT_LONG,

    // Stack operations
    OP_POP,
    OP_DUP,
    OP_SWAP,
    OP_OVER,
    OP_ROT,

    // Local variables
    OP_LOAD,
    OP_STORE,
    OP_LOAD_0,  OP_LOAD_1,  OP_LOAD_2,  OP_LOAD_3,
    OP_STORE_0, OP_STORE_1, OP_STORE_2, OP_STORE_3,

    // Arithmetic
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_NEG, OP_POW,

    // Comparison
    OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE,

    // Bitwise
    OP_BIT_AND, OP_BIT_OR, OP_BIT_XOR, OP_BIT_NOT,
    OP_SHL, OP_SHR,

    // Logical
    OP_NOT,

    // Control flow
    OP_JMP,
    OP_JZ,
    OP_JNZ,
    OP_LOOP,

    // Functions
    OP_CALL,
    OP_RET,

    // Objects
    OP_NEW_TUPLE,
    OP_NEW_TABLE,
    OP_TABLE_GET,
    OP_TABLE_SET,

    // Type
    OP_TYPE,

    // I/O
    OP_PRINT,
    OP_PRINTLN,

    // System
    OP_HALT,
};

struct Chunk {
    std::vector<uint8_t> code;
    std::vector<Value> constants;
    std::vector<int> lines;

    void write(uint8_t byte, int line);
    void writeOpcode(Opcode op, int line);
    int addConstant(Value v);
    void writeConstant(Value v, int line);

    size_t count() const { return code.size(); }
    uint8_t& operator[](size_t i) { return code[i]; }
    const uint8_t& operator[](size_t i) const { return code[i]; }

    ~Chunk();
    void clear();

    // Serialization: write chunk to file (returns false on error)
    bool serialize(FILE* f) const;
    // Deserialization: read chunk from file (returns false on error)
    bool deserialize(FILE* f);
};

struct Function {
    std::string name;
    Chunk chunk;
    int arity = 0;
    int numLocals = 0; // total local slots (includes args)

    // Serialization: write function to file (returns false on error)
    bool serialize(FILE* f) const;
    // Deserialization: read function from file (returns false on error)
    bool deserialize(FILE* f);

    // Write/read a complete program (multiple functions) to/from .mbc file
    static bool writeProgram(const char* path,
                             const std::vector<Function*>& funcs);
    static bool readProgram(const char* path,
                            std::vector<Function*>& outFuncs);
};
