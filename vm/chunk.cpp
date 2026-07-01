#include "chunk.hpp"

Chunk::~Chunk() {
    for (auto& v : constants) {
        if (v.type == ValueType::STRING) {
            delete static_cast<std::string*>(v.ptr);
        }
    }
}

void Chunk::write(uint8_t byte, int line) {
    code.push_back(byte);
    lines.push_back(line);
}

void Chunk::writeOpcode(Opcode op, int line) {
    write(static_cast<uint8_t>(op), line);
}

int Chunk::addConstant(Value v) {
    constants.push_back(v);
    return static_cast<int>(constants.size()) - 1;
}

void Chunk::writeConstant(Value v, int line) {
    int idx = addConstant(v);
    if (idx < 256) {
        writeOpcode(OP_CONSTANT, line);
        write(static_cast<uint8_t>(idx), line);
    } else {
        writeOpcode(OP_CONSTANT_LONG, line);
        write(static_cast<uint8_t>(idx & 0xFF), line);
        write(static_cast<uint8_t>((idx >> 8) & 0xFF), line);
    }
}

void Chunk::clear() {
    code.clear();
    for (auto& v : constants) {
        if (v.type == ValueType::STRING) {
            delete static_cast<std::string*>(v.ptr);
        }
    }
    constants.clear();
    lines.clear();
}
