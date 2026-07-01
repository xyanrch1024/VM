#pragma once
#include "value.hpp"
#include <string>
#include <cstdlib>

struct Function;  // forward decl, defined in chunk.hpp

enum class ObjType : uint8_t {
    STRING,
    TABLE,
    CLOSURE,
    UPVALUE,
};

struct Obj {
    ObjType type;
    bool marked = false;
    Obj* next = nullptr;
};

struct ObjString : Obj {
    std::string str;
    ObjString(const std::string& s) : str(s) { type = ObjType::STRING; }
};

struct ObjTable : Obj {
    ObjTable() { type = ObjType::TABLE; }
};

struct ObjUpvalue;

struct ObjClosure : Obj {
    Function* function = nullptr;
    int upvalueCount = 0;
    ObjUpvalue** upvalues = nullptr;

    ObjClosure() { type = ObjType::CLOSURE; }
};

struct ObjUpvalue : Obj {
    Value* location = nullptr;
    Value closedValue;

    ObjUpvalue() { type = ObjType::UPVALUE; }
};
