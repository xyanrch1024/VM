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
    struct Entry { Value key; Value value; };
    std::vector<Entry> entries;

    Value get(Value key) {
        for (auto& e : entries)
            if (e.key.equals(key)) return e.value;
        return Value::nil();
    }

    void set(Value key, Value value) {
        if (value.type == ValueType::NIL) {
            for (size_t i = 0; i < entries.size(); i++)
                if (entries[i].key.equals(key))
                    { entries.erase(entries.begin() + i); return; }
            return;
        }
        for (auto& e : entries)
            if (e.key.equals(key))
                { e.value = value; return; }
        entries.push_back({key, value});
    }

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
