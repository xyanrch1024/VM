#pragma once
#include <cstdint>
#include <iostream>
#include <string>

enum class ValueType : uint8_t {
    NIL,
    BOOL,
    INT,
    FLOAT,
    STRING,
};

struct Value {
    ValueType type;
    union {
        int64_t integer;
        double floating;
        bool boolean;
        void* ptr;
    };

    static Value nil()   { return {ValueType::NIL,   {.integer = 0}}; }
    static Value makeBool(bool v) { return {ValueType::BOOL,  {.boolean = v}}; }
    static Value makeInt(int64_t v)  { return {ValueType::INT,   {.integer = v}}; }
    static Value makeFloat(double v) { return {ValueType::FLOAT, {.floating = v}}; }
    static Value makeStr(void* p)   { return {ValueType::STRING, {.ptr = p}}; }

    bool isTruthy() const {
        switch (type) {
            case ValueType::NIL:    return false;
            case ValueType::BOOL:   return boolean;
            case ValueType::INT:    return integer != 0;
            case ValueType::FLOAT:  return floating != 0.0;
            case ValueType::STRING: return !static_cast<std::string*>(ptr)->empty();
        }
        return true;
    }

    void print(std::ostream& out = std::cout) const {
        switch (type) {
            case ValueType::NIL:    out << "nil"; break;
            case ValueType::BOOL:   out << (boolean ? "true" : "false"); break;
            case ValueType::INT:    out << integer; break;
            case ValueType::FLOAT:  out << floating; break;
            case ValueType::STRING: out << *static_cast<std::string*>(ptr); break;
        }
    }

    bool equals(const Value& other) const {
        if (type != other.type) return false;
        switch (type) {
            case ValueType::NIL:   return true;
            case ValueType::BOOL:  return boolean == other.boolean;
            case ValueType::INT:   return integer == other.integer;
            case ValueType::FLOAT: return floating == other.floating;
            case ValueType::STRING: return ptr == other.ptr;
        }
        return false;
    }
};
