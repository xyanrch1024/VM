#include "chunk.hpp"
#include <cstring>
#include <cstdint>

// ---- Helper: read/write little-endian scalars ----

static bool write32(FILE* f, uint32_t v) {
    uint8_t buf[4] = { (uint8_t)(v & 0xFF), (uint8_t)((v>>8)&0xFF),
                       (uint8_t)((v>>16)&0xFF), (uint8_t)((v>>24)&0xFF) };
    return fwrite(buf, 4, 1, f) == 1;
}

static bool write64(FILE* f, uint64_t v) {
    uint8_t buf[8];
    for (int i = 0; i < 8; i++) { buf[i] = (uint8_t)(v & 0xFF); v >>= 8; }
    return fwrite(buf, 8, 1, f) == 1;
}

static bool read32(FILE* f, uint32_t& v) {
    uint8_t buf[4];
    if (fread(buf, 4, 1, f) != 1) return false;
    v = (uint32_t)buf[0] | ((uint32_t)buf[1]<<8) | ((uint32_t)buf[2]<<16) | ((uint32_t)buf[3]<<24);
    return true;
}

static bool read64(FILE* f, uint64_t& v) {
    uint8_t buf[8];
    if (fread(buf, 8, 1, f) != 1) return false;
    v = 0;
    for (int i = 7; i >= 0; i--) { v <<= 8; v |= buf[i]; }
    return true;
}

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

// ---- Serialization ----

static const uint32_t MBC_MAGIC = 0x0043424D; // "MBC\0"
static const uint8_t  MBC_VERSION = 0x01;

static bool writeValue(FILE* f, const Value& v) {
    uint8_t tag = (uint8_t)v.type;
    if (fwrite(&tag, 1, 1, f) != 1) return false;
    switch (v.type) {
        case ValueType::NIL: return true;
        case ValueType::BOOL: return fwrite(&v.boolean, 1, 1, f) == 1;
        case ValueType::INT:  return write64(f, (uint64_t)v.integer);
        case ValueType::FLOAT: {
            uint64_t bits;
            memcpy(&bits, &v.floating, 8);
            return write64(f, bits);
        }
        case ValueType::STRING: {
            auto* s = static_cast<std::string*>(v.ptr);
            uint32_t len = (uint32_t)s->size();
            if (!write32(f, len)) return false;
            return fwrite(s->data(), 1, len, f) == len;
        }
        default: return false;
    }
}

static bool readValue(FILE* f, Value& v) {
    uint8_t tag;
    if (fread(&tag, 1, 1, f) != 1) return false;
    v.type = (ValueType)tag;
    switch (v.type) {
        case ValueType::NIL:
            return true;
        case ValueType::BOOL: {
            uint8_t b;
            if (fread(&b, 1, 1, f) != 1) return false;
            v.boolean = (b != 0);
            return true;
        }
        case ValueType::INT: {
            uint64_t bits;
            if (!read64(f, bits)) return false;
            v.integer = (int64_t)bits;
            return true;
        }
        case ValueType::FLOAT: {
            uint64_t bits;
            if (!read64(f, bits)) return false;
            memcpy(&v.floating, &bits, 8);
            return true;
        }
        case ValueType::STRING: {
            uint32_t len;
            if (!read32(f, len)) return false;
            auto* s = new std::string();
            s->resize(len);
            if (fread(&(*s)[0], 1, len, f) != len) { delete s; return false; }
            v.ptr = s;
            return true;
        }
        default: return false;
    }
}

bool Chunk::serialize(FILE* f) const {
    // Constants
    uint32_t n = (uint32_t)constants.size();
    if (!write32(f, n)) return false;
    for (auto& cv : constants)
        if (!writeValue(f, cv)) return false;

    // Code
    n = (uint32_t)code.size();
    if (!write32(f, n)) return false;
    if (n > 0 && fwrite(code.data(), 1, n, f) != n) return false;

    // Lines
    n = (uint32_t)lines.size();
    if (!write32(f, n)) return false;
    for (auto l : lines)
        if (!write32(f, (uint32_t)l)) return false;

    return true;
}

bool Chunk::deserialize(FILE* f) {
    clear();

    // Constants
    uint32_t n;
    if (!read32(f, n)) return false;
    constants.reserve(n);
    for (uint32_t i = 0; i < n; i++) {
        Value v;
        if (!readValue(f, v)) return false;
        constants.push_back(v);
    }

    // Code
    if (!read32(f, n)) return false;
    code.resize(n);
    if (n > 0 && fread(code.data(), 1, n, f) != n) return false;

    // Lines
    if (!read32(f, n)) return false;
    lines.resize(n);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t l;
        if (!read32(f, l)) return false;
        lines[i] = (int)l;
    }

    return true;
}

// ---- Function serialization ----

bool Function::serialize(FILE* f) const {
    // Name
    uint32_t nameLen = (uint32_t)name.size();
    if (!write32(f, nameLen)) return false;
    if (nameLen > 0 && fwrite(name.data(), 1, nameLen, f) != nameLen) return false;

    // Arity & numLocals
    if (fwrite(&arity, 1, 1, f) != 1) return false;
    if (fwrite(&numLocals, 1, 1, f) != 1) return false;

    // Chunk
    return chunk.serialize(f);
}

bool Function::deserialize(FILE* f) {
    // Name
    uint32_t nameLen;
    if (!read32(f, nameLen)) return false;
    name.resize(nameLen);
    if (nameLen > 0 && fread(&name[0], 1, nameLen, f) != nameLen) return false;

    // Arity & numLocals
    if (fread(&arity, 1, 1, f) != 1) return false;
    if (fread(&numLocals, 1, 1, f) != 1) return false;

    // Chunk
    return chunk.deserialize(f);
}

bool Function::writeProgram(const char* path,
                             const std::vector<Function*>& funcs) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;

    bool ok = true;
    // Magic + version
    ok = ok && write32(f, MBC_MAGIC);
    ok = ok && fwrite(&MBC_VERSION, 1, 1, f) == 1;

    // Function count
    ok = ok && write32(f, (uint32_t)funcs.size());

    for (auto* fn : funcs)
        ok = ok && fn->serialize(f);

    fclose(f);
    return ok;
}

bool Function::readProgram(const char* path,
                            std::vector<Function*>& outFuncs) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    bool ok = true;
    // Magic
    uint32_t magic;
    ok = ok && read32(f, magic);
    ok = ok && (magic == MBC_MAGIC);

    // Version
    uint8_t version;
    ok = ok && (fread(&version, 1, 1, f) == 1);
    ok = ok && (version == MBC_VERSION);

    // Function count
    uint32_t funcCount;
    ok = ok && read32(f, funcCount);

    for (uint32_t i = 0; i < funcCount && ok; i++) {
        auto* fn = new Function();
        ok = fn->deserialize(f);
        if (ok) outFuncs.push_back(fn);
        else delete fn;
    }

    fclose(f);
    return ok && outFuncs.size() == funcCount;
}
