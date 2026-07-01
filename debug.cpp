#include "debug.hpp"
#include <cstdio>
#include <cstdarg>

static int simpleInstruction(const char* name, int offset) {
    printf("  %s\n", name);
    return offset + 1;
}

static int byteInstruction(const char* name, int offset, const Chunk& chunk) {
    uint8_t slot = chunk.code[offset + 1];
    printf("  %-16s %4d\n", name, slot);
    return offset + 2;
}

static int shortInstruction(const char* name, int offset, const Chunk& chunk) {
    uint16_t val = (uint16_t)(chunk.code[offset + 1]) |
                   ((uint16_t)(chunk.code[offset + 2]) << 8);
    printf("  %-16s %4d\n", name, (int16_t)val);
    return offset + 3;
}

static int constantInstruction(const char* name, int offset, const Chunk& chunk) {
    uint8_t idx = chunk.code[offset + 1];
    printf("  %-16s %4d '", name, idx);
    chunk.constants[idx].print();
    printf("'\n");
    return offset + 2;
}

static int constantLongInstruction(const char* name, int offset, const Chunk& chunk) {
    uint16_t idx = (uint16_t)(chunk.code[offset + 1]) |
                   ((uint16_t)(chunk.code[offset + 2]) << 8);
    printf("  %-16s %4d '", name, idx);
    chunk.constants[idx].print();
    printf("'\n");
    return offset + 3;
}

int disassembleInstruction(const Chunk& chunk, int offset) {
    printf("%04d  ", offset);
    if (offset > 0 && chunk.lines[offset] == chunk.lines[offset - 1]) {
        printf("   | ");
    } else {
        printf("%4d ", chunk.lines[offset]);
    }

    uint8_t instr = chunk.code[offset];
    switch (instr) {
        case OP_NIL:            return simpleInstruction("OP_NIL", offset);
        case OP_TRUE:           return simpleInstruction("OP_TRUE", offset);
        case OP_FALSE:          return simpleInstruction("OP_FALSE", offset);
        case OP_CONSTANT:       return constantInstruction("OP_CONSTANT", offset, chunk);
        case OP_CONSTANT_LONG:  return constantLongInstruction("OP_CONSTANT_LONG", offset, chunk);
        case OP_POP:            return simpleInstruction("OP_POP", offset);
        case OP_DUP:            return simpleInstruction("OP_DUP", offset);
        case OP_SWAP:           return simpleInstruction("OP_SWAP", offset);
        case OP_OVER:           return simpleInstruction("OP_OVER", offset);
        case OP_ROT:            return simpleInstruction("OP_ROT", offset);
        case OP_LOAD:           return byteInstruction("OP_LOAD", offset, chunk);
        case OP_STORE:          return byteInstruction("OP_STORE", offset, chunk);
        case OP_LOAD_0:         return simpleInstruction("OP_LOAD_0", offset);
        case OP_LOAD_1:         return simpleInstruction("OP_LOAD_1", offset);
        case OP_LOAD_2:         return simpleInstruction("OP_LOAD_2", offset);
        case OP_LOAD_3:         return simpleInstruction("OP_LOAD_3", offset);
        case OP_STORE_0:        return simpleInstruction("OP_STORE_0", offset);
        case OP_STORE_1:        return simpleInstruction("OP_STORE_1", offset);
        case OP_STORE_2:        return simpleInstruction("OP_STORE_2", offset);
        case OP_STORE_3:        return simpleInstruction("OP_STORE_3", offset);
        case OP_ADD:            return simpleInstruction("OP_ADD", offset);
        case OP_SUB:            return simpleInstruction("OP_SUB", offset);
        case OP_MUL:            return simpleInstruction("OP_MUL", offset);
        case OP_DIV:            return simpleInstruction("OP_DIV", offset);
        case OP_MOD:            return simpleInstruction("OP_MOD", offset);
        case OP_NEG:            return simpleInstruction("OP_NEG", offset);
        case OP_POW:            return simpleInstruction("OP_POW", offset);
        case OP_EQ:             return simpleInstruction("OP_EQ", offset);
        case OP_NE:             return simpleInstruction("OP_NE", offset);
        case OP_LT:             return simpleInstruction("OP_LT", offset);
        case OP_GT:             return simpleInstruction("OP_GT", offset);
        case OP_LE:             return simpleInstruction("OP_LE", offset);
        case OP_GE:             return simpleInstruction("OP_GE", offset);
        case OP_BIT_AND:        return simpleInstruction("OP_BIT_AND", offset);
        case OP_BIT_OR:         return simpleInstruction("OP_BIT_OR", offset);
        case OP_BIT_XOR:        return simpleInstruction("OP_BIT_XOR", offset);
        case OP_BIT_NOT:        return simpleInstruction("OP_BIT_NOT", offset);
        case OP_SHL:            return simpleInstruction("OP_SHL", offset);
        case OP_SHR:            return simpleInstruction("OP_SHR", offset);
        case OP_NOT:            return simpleInstruction("OP_NOT", offset);
        case OP_JMP:            return shortInstruction("OP_JMP", offset, chunk);
        case OP_JZ:             return shortInstruction("OP_JZ", offset, chunk);
        case OP_JNZ:            return shortInstruction("OP_JNZ", offset, chunk);
        case OP_LOOP:           return shortInstruction("OP_LOOP", offset, chunk);
        case OP_CALL:           return byteInstruction("OP_CALL", offset, chunk);
        case OP_RET:            return simpleInstruction("OP_RET", offset);
        case OP_NEW_TUPLE:      return byteInstruction("OP_NEW_TUPLE", offset, chunk);
        case OP_PRINT:          return simpleInstruction("OP_PRINT", offset);
        case OP_PRINTLN:        return simpleInstruction("OP_PRINTLN", offset);
        case OP_HALT:           return simpleInstruction("OP_HALT", offset);
        default:
            printf("  Unknown opcode %d\n", instr);
            return offset + 1;
    }
}

void disassembleChunk(const Chunk& chunk, const char* name) {
    printf("== %s ==\n", name);
    int offset = 0;
    while (offset < (int)chunk.count()) {
        offset = disassembleInstruction(chunk, offset);
    }
    printf("\n");
}
