#include "mbs.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <cstdlib>
#include <cmath>

// ============================================================
// Helpers
// ============================================================

static std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && isspace((unsigned char)s[start])) start++;
    size_t end = s.size();
    while (end > start && isspace((unsigned char)s[end-1])) end--;
    return s.substr(start, end - start);
}

static std::vector<std::string> splitWS(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && isspace((unsigned char)s[i])) i++;
        if (i >= s.size()) break;
        // Check for quoted string
        if (s[i] == '"') {
            size_t j = i + 1;
            while (j < s.size() && (s[j] != '"' || s[j-1] == '\\')) j++;
            if (j < s.size()) j++; // closing quote
            out.push_back(s.substr(i, j - i));
            i = j;
        } else {
            size_t j = i;
            while (j < s.size() && !isspace((unsigned char)s[j])) j++;
            out.push_back(s.substr(i, j - i));
            i = j;
        }
    }
    return out;
}

// Strip comment (; ...) from a line, respecting quoted strings
static std::string stripComment(const std::string& line) {
    bool inString = false;
    for (size_t i = 0; i < line.size(); i++) {
        if (line[i] == '"' && (i == 0 || line[i-1] != '\\'))
            inString = !inString;
        if (!inString && line[i] == ';')
            return line.substr(0, i);
    }
    return line;
}

static std::string escapeString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (char c : s) {
        switch (c) {
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            default:   out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

static std::string unescapeString(const std::string& s) {
    if (s.size() < 2 || s.front() != '"' || s.back() != '"')
        return s;
    std::string out;
    out.reserve(s.size());
    for (size_t i = 1; i < s.size() - 1; i++) {
        if (s[i] == '\\' && i + 1 < s.size() - 1) {
            switch (s[i+1]) {
                case 'n': out.push_back('\n'); i++; break;
                case 't': out.push_back('\t'); i++; break;
                case 'r': out.push_back('\r'); i++; break;
                case '\\': out.push_back('\\'); i++; break;
                case '"':  out.push_back('"');  i++; break;
                default:   out.push_back(s[i]);
            }
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

// Format a double without unnecessary trailing zeros
static std::string formatFloat(double v) {
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
    char buf[64];
    snprintf(buf, sizeof(buf), "%.17g", v);
    // Ensure it has '.' or 'e' so it's recognized as float on reassembly
    std::string s(buf);
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos && s.find('E') == std::string::npos)
        s += ".0";
    return s;
}

// ============================================================
// Instruction info
// ============================================================

enum class OperandType { NONE, BYTE, SHORT, CONST_IDX, LABEL };

static int insnSize(uint8_t op) {
    switch (op) {
        case OP_NIL: case OP_TRUE: case OP_FALSE:
        case OP_POP: case OP_DUP: case OP_SWAP: case OP_OVER: case OP_ROT:
        case OP_LOAD_0: case OP_LOAD_1: case OP_LOAD_2: case OP_LOAD_3:
        case OP_STORE_0: case OP_STORE_1: case OP_STORE_2: case OP_STORE_3:
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD: case OP_NEG: case OP_POW:
        case OP_EQ: case OP_NE: case OP_LT: case OP_GT: case OP_LE: case OP_GE:
        case OP_BIT_AND: case OP_BIT_OR: case OP_BIT_XOR: case OP_BIT_NOT:
        case OP_SHL: case OP_SHR:
        case OP_NOT: case OP_RET: case OP_PRINT: case OP_PRINTLN: case OP_HALT:
        case OP_NEW_TABLE: case OP_TABLE_GET: case OP_TABLE_SET: case OP_TYPE:
            return 1;
        case OP_CONSTANT: case OP_LOAD: case OP_STORE: case OP_CALL: case OP_NEW_TUPLE:
        case OP_GET_UPVALUE: case OP_SET_UPVALUE:
            return 2;
        case OP_CONSTANT_LONG: case OP_JMP: case OP_JZ: case OP_JNZ: case OP_LOOP:
            return 3;
        case OP_CLOSURE:
            return -1; // variable length
        default:
            return 1;
    }
}

static const char* opcodeMnemonic(uint8_t op) {
    switch (op) {
        case OP_NIL:            return "nil";
        case OP_TRUE:           return "true";
        case OP_FALSE:          return "false";
        case OP_CONSTANT:       return "constant";
        case OP_CONSTANT_LONG:  return "constant_long";
        case OP_POP:            return "pop";
        case OP_DUP:            return "dup";
        case OP_SWAP:           return "swap";
        case OP_OVER:           return "over";
        case OP_ROT:            return "rot";
        case OP_LOAD:           return "load";
        case OP_STORE:          return "store";
        case OP_LOAD_0:         return "load_0";
        case OP_LOAD_1:         return "load_1";
        case OP_LOAD_2:         return "load_2";
        case OP_LOAD_3:         return "load_3";
        case OP_STORE_0:        return "store_0";
        case OP_STORE_1:        return "store_1";
        case OP_STORE_2:        return "store_2";
        case OP_STORE_3:        return "store_3";
        case OP_ADD:            return "add";
        case OP_SUB:            return "sub";
        case OP_MUL:            return "mul";
        case OP_DIV:            return "div";
        case OP_MOD:            return "mod";
        case OP_NEG:            return "neg";
        case OP_POW:            return "pow";
        case OP_EQ:             return "eq";
        case OP_NE:             return "ne";
        case OP_LT:             return "lt";
        case OP_GT:             return "gt";
        case OP_LE:             return "le";
        case OP_GE:             return "ge";
        case OP_BIT_AND:        return "bit_and";
        case OP_BIT_OR:         return "bit_or";
        case OP_BIT_XOR:        return "bit_xor";
        case OP_BIT_NOT:        return "bit_not";
        case OP_SHL:            return "shl";
        case OP_SHR:            return "shr";
        case OP_NOT:            return "not";
        case OP_JMP:            return "jmp";
        case OP_JZ:             return "jz";
        case OP_JNZ:            return "jnz";
        case OP_LOOP:           return "loop";
        case OP_CALL:           return "call";
        case OP_RET:            return "ret";
        case OP_CLOSURE:        return "closure";
        case OP_GET_UPVALUE:    return "get_upvalue";
        case OP_SET_UPVALUE:    return "set_upvalue";
        case OP_NEW_TUPLE:      return "new_tuple";
        case OP_NEW_TABLE:      return "new_table";
        case OP_TABLE_GET:      return "table_get";
        case OP_TABLE_SET:      return "table_set";
        case OP_TYPE:           return "type";
        case OP_PRINT:          return "print";
        case OP_PRINTLN:        return "println";
        case OP_HALT:           return "halt";
        default:                return "???";
    }
}

static OperandType opcodeOperand(uint8_t op) {
    switch (op) {
        case OP_CONSTANT:       return OperandType::CONST_IDX;
        case OP_CONSTANT_LONG:  return OperandType::CONST_IDX;
        case OP_LOAD:           return OperandType::BYTE;
        case OP_STORE:          return OperandType::BYTE;
        case OP_CALL:           return OperandType::BYTE;
        case OP_NEW_TUPLE:      return OperandType::BYTE;
        case OP_GET_UPVALUE:    return OperandType::BYTE;
        case OP_SET_UPVALUE:    return OperandType::BYTE;
        case OP_JMP:            return OperandType::LABEL;
        case OP_JZ:             return OperandType::LABEL;
        case OP_JNZ:            return OperandType::LABEL;
        case OP_LOOP:           return OperandType::LABEL;
        default:                return OperandType::NONE;
    }
}

// Lookup opcode from mnemonic string, return -1 on failure
static int lookupMnemonic(const std::string& name) {
    static const std::pair<const char*, uint8_t> table[] = {
        {"nil", OP_NIL}, {"true", OP_TRUE}, {"false", OP_FALSE},
        {"constant", OP_CONSTANT}, {"constant_long", OP_CONSTANT_LONG},
        {"pop", OP_POP}, {"dup", OP_DUP}, {"swap", OP_SWAP},
        {"over", OP_OVER}, {"rot", OP_ROT},
        {"load", OP_LOAD}, {"store", OP_STORE},
        {"load_0", OP_LOAD_0}, {"load_1", OP_LOAD_1},
        {"load_2", OP_LOAD_2}, {"load_3", OP_LOAD_3},
        {"store_0", OP_STORE_0}, {"store_1", OP_STORE_1},
        {"store_2", OP_STORE_2}, {"store_3", OP_STORE_3},
        {"add", OP_ADD}, {"sub", OP_SUB}, {"mul", OP_MUL},
        {"div", OP_DIV}, {"mod", OP_MOD}, {"neg", OP_NEG}, {"pow", OP_POW},
        {"eq", OP_EQ}, {"ne", OP_NE}, {"lt", OP_LT}, {"gt", OP_GT},
        {"le", OP_LE}, {"ge", OP_GE},
        {"bit_and", OP_BIT_AND}, {"bit_or", OP_BIT_OR},
        {"bit_xor", OP_BIT_XOR}, {"bit_not", OP_BIT_NOT},
        {"shl", OP_SHL}, {"shr", OP_SHR},
        {"not", OP_NOT},
        {"jmp", OP_JMP}, {"jz", OP_JZ}, {"jnz", OP_JNZ}, {"loop", OP_LOOP},
        {"call", OP_CALL}, {"ret", OP_RET},
        {"closure", OP_CLOSURE},
        {"get_upvalue", OP_GET_UPVALUE}, {"set_upvalue", OP_SET_UPVALUE},
        {"new_tuple", OP_NEW_TUPLE},
        {"new_table", OP_NEW_TABLE}, {"table_get", OP_TABLE_GET},
        {"table_set", OP_TABLE_SET}, {"type", OP_TYPE},
        {"print", OP_PRINT}, {"println", OP_PRINTLN}, {"halt", OP_HALT},
    };
    for (auto& entry : table) {
        if (name == entry.first)
            return entry.second;
    }
    return -1;
}

// ============================================================
// Disassembler
// ============================================================

struct JumpTarget {
    int offset;
    std::string label;
};

static std::vector<JumpTarget> findJumpTargets(const Chunk& chunk) {
    std::vector<JumpTarget> targets;
    int offset = 0;
    while (offset < (int)chunk.code.size()) {
        uint8_t op = chunk.code[offset];
        if (op == OP_JMP || op == OP_JZ || op == OP_JNZ || op == OP_LOOP) {
            if (offset + 2 >= (int)chunk.code.size()) break;
            int16_t disp = (int16_t)(chunk.code[offset+1] | (chunk.code[offset+2] << 8));
            int target;
            if (op == OP_LOOP)
                target = offset + 3 - disp;  // PC -= disp
            else
                target = offset + 3 + disp;  // PC += disp
            bool found = false;
            for (auto& t : targets)
                if (t.offset == target) { found = true; break; }
            if (!found)
                targets.push_back({target, ""});
        }
        offset += insnSize(op);
    }
    std::sort(targets.begin(), targets.end(),
        [](const JumpTarget& a, const JumpTarget& b) { return a.offset < b.offset; });
    for (size_t i = 0; i < targets.size(); i++)
        targets[i].label = "@L" + std::to_string(i);
    return targets;
}

static std::string labelForOffset(const std::vector<JumpTarget>& targets, int offset) {
    for (auto& t : targets)
        if (t.offset == offset) return t.label;
    return "";
}

std::string disassembleToText(const Function& func) {
    std::ostringstream out;

    // Header
    out << ".function " << func.name
        << " arity=" << func.arity
        << " locals=" << func.numLocals << "\n";

    // Constants
    for (size_t i = 0; i < func.chunk.constants.size(); i++) {
        const Value& v = func.chunk.constants[i];
        out << "  ";
        switch (v.type) {
            case ValueType::NIL:    out << ".nil"; break;
            case ValueType::BOOL:   out << ".bool " << (v.boolean ? "true" : "false"); break;
            case ValueType::INT:    out << ".int " << v.integer; break;
            case ValueType::FLOAT:  out << ".float " << formatFloat(v.floating); break;
            case ValueType::STRING: out << ".string " << escapeString(*static_cast<std::string*>(v.ptr)); break;
        }
        out << "  ; const " << i << "\n";
    }

    // Jump targets
    auto targets = findJumpTargets(func.chunk);

    // Code
    int offset = 0;
    while (offset < (int)func.chunk.code.size()) {
        // Label?
        std::string lbl = labelForOffset(targets, offset);
        if (!lbl.empty())
            out << lbl << ":\n";

        uint8_t op = func.chunk.code[offset];
        out << "  " << opcodeMnemonic(op);

        OperandType ot = opcodeOperand(op);
        if (ot == OperandType::CONST_IDX) {
            int idx;
            if (op == OP_CONSTANT)
                idx = func.chunk.code[offset + 1];
            else
                idx = func.chunk.code[offset + 1] | (func.chunk.code[offset + 2] << 8);
            out << " " << idx;
            if (idx < (int)func.chunk.constants.size()) {
                out << "  ; ";
                func.chunk.constants[idx].print(out);
            }
        } else if (ot == OperandType::BYTE) {
            int val = func.chunk.code[offset + 1];
            out << " " << val;
        } else if (ot == OperandType::LABEL) {
            int16_t disp = (int16_t)(func.chunk.code[offset+1] | (func.chunk.code[offset+2] << 8));
            int target;
            if (op == OP_LOOP)
                target = offset + 3 - disp;
            else
                target = offset + 3 + disp;
            std::string l = labelForOffset(targets, target);
            out << " " << (l.empty() ? "???" : l);
        }

        if (op == OP_CLOSURE) {
            int funcIdx = func.chunk.code[offset+1] | (func.chunk.code[offset+2] << 8);
            int uvCount = func.chunk.code[offset+3];
            out << " " << funcIdx << " " << uvCount;
            for (int i = 0; i < uvCount; i++) {
                out << " " << (int)func.chunk.code[offset+4+i];
            }
            offset += 4 + uvCount;
        } else {
            offset += insnSize(op);
        }
    }

    out << ".end\n";
    return out.str();
}

std::string disassembleProgramToText(const std::vector<Function*>& funcs) {
    std::string result;
    for (auto* fn : funcs) {
        result += disassembleToText(*fn);
        result += "\n";
    }
    return result;
}

// ============================================================
// Assembler: intermediate representation
// ============================================================

enum class AsmKind {
    LABEL,      // label-only line
    FUNC_HEADER,
    FUNC_END,
    CONST_NIL,
    CONST_BOOL,
    CONST_INT,
    CONST_FLOAT,
    CONST_STRING,
    INSN,
};

struct AsmLine {
    std::string label;      // label defined on this line (empty if none)
    AsmKind kind = AsmKind::LABEL;
    std::string funcName;
    int arity = 0;
    int numLocals = 0;
    Value constValue;
    std::string mnemonic;
    std::vector<std::string> args;  // raw arg strings
};

// ============================================================
// Assembler
// ============================================================

bool assembleFromText(const std::string& text, std::vector<Function*>& outFuncs, std::string& outError) {
    outFuncs.clear();

    // ---- Parse lines into AsmLine list ----
    std::vector<AsmLine> lines;
    std::string currentFuncName;

    std::istringstream stream(text);
    std::string rawLine;
    while (std::getline(stream, rawLine)) {
        std::string trimmed = trim(rawLine);
        std::string clean = stripComment(trimmed);
        clean = trim(clean);
        if (clean.empty()) continue;

        AsmLine al;

        // Check for label at start of line (e.g. "@foo:" or "foo:")
        size_t pos = 0;
        // Label pattern: optional @ then alphanum/underscore, then :
        {
            size_t colon = clean.find(':');
            if (colon != std::string::npos) {
                std::string maybeLabel = clean.substr(0, colon);
                bool valid = !maybeLabel.empty();
                for (char c : maybeLabel)
                    if (!isalnum((unsigned char)c) && c != '_' && c != '@')
                        valid = false;
                if (valid) {
                    al.label = maybeLabel;
                    if (al.label.front() == '@')
                        al.label = al.label.substr(1); // strip @ prefix
                    clean = trim(clean.substr(colon + 1));
                }
            }
        }

        if (clean.empty()) {
            // Just a label on its own line
            al.kind = AsmKind::LABEL;
            lines.push_back(al);
            continue;
        }

        auto tokens = splitWS(clean);
        if (tokens.empty()) continue;

        const std::string& first = tokens[0];

        if (first == ".function") {
            al.kind = AsmKind::FUNC_HEADER;
            if (tokens.size() >= 2)
                al.funcName = tokens[1];
            else {
                outError = ".function requires a name";
                return false;
            }
            // Parse key=value pairs
            for (size_t i = 2; i < tokens.size(); i++) {
                auto eq = tokens[i].find('=');
                if (eq == std::string::npos) continue;
                std::string key = tokens[i].substr(0, eq);
                std::string val = tokens[i].substr(eq + 1);
                if (key == "arity") al.arity = atoi(val.c_str());
                else if (key == "locals") al.numLocals = atoi(val.c_str());
            }
            currentFuncName = al.funcName;

        } else if (first == ".end") {
            al.kind = AsmKind::FUNC_END;

        } else if (first == ".nil") {
            al.kind = AsmKind::CONST_NIL;
            al.constValue = Value::nil();

        } else if (first == ".bool") {
            al.kind = AsmKind::CONST_BOOL;
            if (tokens.size() < 2 || (tokens[1] != "true" && tokens[1] != "false")) {
                outError = ".bool requires true or false";
                return false;
            }
            al.constValue = Value::makeBool(tokens[1] == "true");

        } else if (first == ".int") {
            al.kind = AsmKind::CONST_INT;
            if (tokens.size() < 2) {
                outError = ".int requires a value";
                return false;
            }
            char* end;
            long long v = strtoll(tokens[1].c_str(), &end, 10);
            if (*end != '\0') {
                outError = "invalid integer: " + tokens[1];
                return false;
            }
            al.constValue = Value::makeInt((int64_t)v);

        } else if (first == ".float") {
            al.kind = AsmKind::CONST_FLOAT;
            if (tokens.size() < 2) {
                outError = ".float requires a value";
                return false;
            }
            char* end;
            double v = strtod(tokens[1].c_str(), &end);
            if (*end != '\0') {
                outError = "invalid float: " + tokens[1];
                return false;
            }
            al.constValue = Value::makeFloat(v);

        } else if (first == ".string") {
            al.kind = AsmKind::CONST_STRING;
            if (tokens.size() < 2) {
                outError = ".string requires a quoted value";
                return false;
            }
            std::string unesc = unescapeString(tokens[1]);
            al.constValue = Value::makeStr(new std::string(unesc));

        } else {
            // Instruction
            al.kind = AsmKind::INSN;
            al.mnemonic = first;
            for (size_t i = 1; i < tokens.size(); i++)
                al.args.push_back(tokens[i]);
        }

        lines.push_back(al);
    }

    // ---- Process into functions ----
    size_t lineIdx = 0;
    while (lineIdx < lines.size()) {
        // Skip lines that are just labels (no header before them)
        while (lineIdx < lines.size() && lines[lineIdx].kind != AsmKind::FUNC_HEADER)
            lineIdx++;
        if (lineIdx >= lines.size()) break;

        auto* func = new Function();
        func->name = lines[lineIdx].funcName;
        func->arity = lines[lineIdx].arity;
        func->numLocals = lines[lineIdx].numLocals;
        lineIdx++;

        // Collect constants
        while (lineIdx < lines.size() && lines[lineIdx].kind >= AsmKind::CONST_NIL
               && lines[lineIdx].kind <= AsmKind::CONST_STRING) {
            func->chunk.addConstant(lines[lineIdx].constValue);
            lineIdx++;
        }

        // Collect instruction lines for this function
        std::vector<AsmLine> insnLines;
        while (lineIdx < lines.size() && lines[lineIdx].kind != AsmKind::FUNC_END) {
            if (lines[lineIdx].kind == AsmKind::INSN || !lines[lineIdx].label.empty())
                insnLines.push_back(lines[lineIdx]);
            lineIdx++;
        }
        if (lineIdx < lines.size() && lines[lineIdx].kind == AsmKind::FUNC_END)
            lineIdx++; // skip .end

        // ---- Two-pass assembly ----
        // Pass 1: compute label offsets and record patch points
        std::unordered_map<std::string, int> labelOffsets;
        struct Patch {
            int codePos;    // position in code vector (first operand byte)
            std::string label;
            uint8_t opcode; // the opcode (for + vs - offset)
        };
        std::vector<Patch> patches;

        int codeOffset = 0;
        for (auto& al : insnLines) {
            if (!al.label.empty()) {
                if (labelOffsets.count(al.label)) {
                    delete func; // clean up
                    outError = "duplicate label '" + al.label + "'";
                    return false;
                }
                labelOffsets[al.label] = codeOffset;
            }
            if (al.kind == AsmKind::INSN) {
                int op = lookupMnemonic(al.mnemonic);
                if (op < 0) {
                    delete func;
                    outError = "unknown instruction '" + al.mnemonic + "'";
                    return false;
                }
                OperandType ot = opcodeOperand((uint8_t)op);
                int size = insnSize((uint8_t)op);

                if (op == OP_CLOSURE) {
                    if (al.args.size() < 2) {
                        delete func;
                        outError = "closure requires at least 2 operands (funcIdx, uvCount)";
                        return false;
                    }
                    size = 4 + (int)strtol(al.args[1].c_str(), nullptr, 10);
                } else if (ot == OperandType::LABEL) {
                    if (al.args.empty()) {
                        delete func;
                        outError = al.mnemonic + " requires a label operand";
                        return false;
                    }
                    patches.push_back({codeOffset + 1, al.args[0], (uint8_t)op});
                } else if (ot == OperandType::CONST_IDX || ot == OperandType::BYTE) {
                    if (al.args.empty()) {
                        delete func;
                        outError = al.mnemonic + " requires an operand";
                        return false;
                    }
                    // Validate numeric
                    char* end;
                    strtol(al.args[0].c_str(), &end, 10);
                    if (*end != '\0') {
                        delete func;
                        outError = al.mnemonic + ": expected numeric operand, got '" + al.args[0] + "'";
                        return false;
                    }
                }

                codeOffset += size;
            }
        }

        // Pass 2: emit bytecode
        Chunk& chunk = func->chunk;
        int lineNum = 1; // Use line 1 for all assembled opcodes
        for (auto& al : insnLines) {
            if (al.kind == AsmKind::INSN) {
                int op = lookupMnemonic(al.mnemonic);
                OperandType ot = opcodeOperand((uint8_t)op);

                chunk.writeOpcode((Opcode)op, lineNum);

                if (op == OP_CLOSURE) {
                    long funcIdx = strtol(al.args[0].c_str(), nullptr, 10);
                    long uvCount = strtol(al.args[1].c_str(), nullptr, 10);
                    chunk.write((uint8_t)(funcIdx & 0xFF), lineNum);
                    chunk.write((uint8_t)((funcIdx >> 8) & 0xFF), lineNum);
                    chunk.write((uint8_t)uvCount, lineNum);
                    for (int i = 2; i < (int)al.args.size(); i++) {
                        long desc = strtol(al.args[i].c_str(), nullptr, 10);
                        chunk.write((uint8_t)desc, lineNum);
                    }
                } else if (ot == OperandType::CONST_IDX) {
                    long idx = strtol(al.args[0].c_str(), nullptr, 10);
                    if (idx >= 256) {
                        chunk.write((uint8_t)(idx & 0xFF), lineNum);
                        chunk.write((uint8_t)((idx >> 8) & 0xFF), lineNum);
                    } else {
                        chunk.write((uint8_t)idx, lineNum);
                    }
                } else if (ot == OperandType::BYTE) {
                    long val = strtol(al.args[0].c_str(), nullptr, 10);
                    chunk.write((uint8_t)val, lineNum);
                } else if (ot == OperandType::LABEL) {
                    // Write placeholder; will be patched below
                    chunk.write(0, lineNum);
                    chunk.write(0, lineNum);
                }
            }
        }

    // Resolve patches
    for (auto& p : patches) {
        // Strip @ prefix from label references for lookup
        std::string lookupLabel = p.label;
        if (!lookupLabel.empty() && lookupLabel.front() == '@')
            lookupLabel = lookupLabel.substr(1);
        auto it = labelOffsets.find(lookupLabel);
            if (it == labelOffsets.end()) {
                delete func;
                outError = "undefined label '" + p.label + "'";
                return false;
            }
            int target = it->second;
            // opcode at p.codePos-1; after opcode read PC=p.codePos;
            // after readShort (2 bytes) PC=p.codePos+2
            // For forward jumps (jmp/jz/jnz): VM does PC += offset
            //   offset = target - (p.codePos + 2)
            // For backward jumps (loop): VM does PC -= offset
            //   offset = (p.codePos + 2) - target
            int16_t offset;
            if (p.opcode == OP_LOOP)
                offset = (int16_t)((p.codePos + 2) - target);
            else
                offset = (int16_t)(target - (p.codePos + 2));
            chunk.code[p.codePos]     = (uint8_t)(offset & 0xFF);
            chunk.code[p.codePos + 1] = (uint8_t)((offset >> 8) & 0xFF);
        }

        outFuncs.push_back(func);
    }

    if (outFuncs.empty()) {
        outError = "no functions found";
        return false;
    }
    return true;
}
