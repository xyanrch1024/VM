#include "vm.hpp"
#include "debug.hpp"
#include <cstdio>
#include <cstdarg>
#include <cmath>

VM::VM() { resetStack(); }
VM::~VM() {}

Function* VM::newFunction(const std::string& name, int arity) {
    auto func = std::make_unique<Function>();
    func->name = name;
    func->arity = arity;
    Function* ptr = func.get();
    functions.push_back(std::move(func));
    return ptr;
}

void VM::addFunction(Function* func) { (void)func; }
Function* VM::getFunction(int index) { return functions[index].get(); }

std::string* VM::internString(const std::string& s) {
    auto it = stringMap.find(s);
    if (it != stringMap.end()) return it->second;
    auto ptr = std::make_unique<std::string>(s);
    auto raw = ptr.get();
    stringMap[*raw] = raw;
    stringTable.push_back(std::move(ptr));
    return raw;
}

std::string* VM::concatStrings(const std::string& a, const std::string& b) {
    return internString(a + b);
}

void VM::resetStack() { stack.clear(); frames.clear(); }
void VM::push(Value v) { stack.push_back(v); }

Value VM::pop() {
    if (stack.empty()) { runtimeError("Stack underflow"); return Value::nil(); }
    Value v = stack.back();
    stack.pop_back();
    return v;
}

Value VM::peek(int distance) { return stack[stack.size() - 1 - distance]; }

void VM::dumpStack() const {
    printf("Stack [%zu]: ", stack.size());
    for (size_t i = 0; i < stack.size(); i++) {
        stack[i].print();
        printf(" ");
    }
    printf("\n");
}

void VM::runtimeError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
    for (int i = (int)frames.size() - 1; i >= 0; i--) {
        int instr = frames[i].pc - 1;
        if (instr >= 0) {
            fprintf(stderr, "[line %d] in %s()\n",
                    frames[i].function->chunk.lines[instr],
                    frames[i].function->name.c_str());
        }
    }
    resetStack();
}

static ValueType promote(ValueType a, ValueType b) {
    if (a == ValueType::FLOAT || b == ValueType::FLOAT) return ValueType::FLOAT;
    return ValueType::INT;
}

static double asNum(const Value& v) {
    return v.type == ValueType::FLOAT ? v.floating : (double)v.integer;
}

static Value makeNum(double d, ValueType t) {
    return t == ValueType::FLOAT ? Value::makeFloat(d) : Value::makeInt((int64_t)d);
}

VM::Result VM::interpret(Function* func) {
    frames.clear();
    CallFrame cf;
    cf.function = func;
    cf.pc = 0;
    cf.fp = 0;
    frames.push_back(cf);
    // Pre-allocate local variable slots for the top-level function
    int localEnd = cf.fp + func->numLocals;
    while ((int)stack.size() < localEnd)
        stack.push_back(Value::nil());

    for (;;) {
#ifdef DEBUG_TRACE
        printf("          ");
        dumpStack();
        disassembleInstruction(frame()->function->chunk, frame()->pc);
#endif

        uint8_t instr = readByte();

        switch (instr) {
            // === Constants ===
            case OP_NIL:    push(Value::nil()); break;
            case OP_TRUE:   push(Value::makeBool(true)); break;
            case OP_FALSE:  push(Value::makeBool(false)); break;

            case OP_CONSTANT:
            case OP_CONSTANT_LONG: {
                Value v = readConstant();
                if (v.type == ValueType::STRING) {
                    v = Value::makeStr(internString(*static_cast<std::string*>(v.ptr)));
                }
                push(v);
                break;
            }

            // === Stack operations ===
            case OP_POP:  pop(); break;
            case OP_DUP:  push(peek()); break;
            case OP_SWAP: { Value a=pop(); Value b=pop(); push(a); push(b); break; }
            case OP_OVER: { Value a=peek(1); push(a); break; }
            case OP_ROT:  { Value a=pop(); Value b=pop(); Value c=pop(); push(b); push(a); push(c); break; }

            // === Local variables ===
            case OP_LOAD:  push(stack[frame()->fp + readByte()]); break;
            case OP_STORE: stack[frame()->fp + readByte()] = peek(); break;
            case OP_LOAD_0:  push(stack[frame()->fp + 0]); break;
            case OP_LOAD_1:  push(stack[frame()->fp + 1]); break;
            case OP_LOAD_2:  push(stack[frame()->fp + 2]); break;
            case OP_LOAD_3:  push(stack[frame()->fp + 3]); break;
            case OP_STORE_0: stack[frame()->fp + 0] = peek(); break;
            case OP_STORE_1: stack[frame()->fp + 1] = peek(); break;
            case OP_STORE_2: stack[frame()->fp + 2] = peek(); break;
            case OP_STORE_3: stack[frame()->fp + 3] = peek(); break;

            // === Arithmetic ===
            case OP_ADD: {
                Value b = pop(); Value a = pop();
                if (a.type == ValueType::STRING && b.type == ValueType::STRING)
                    push(Value::makeStr(concatStrings(*static_cast<std::string*>(a.ptr), *static_cast<std::string*>(b.ptr))));
                else if (a.type == ValueType::INT && b.type == ValueType::INT)
                    push(Value::makeInt(a.integer + b.integer));
                else { ValueType t = promote(a.type, b.type); push(makeNum(asNum(a)+asNum(b), t)); }
                break;
            }
            case OP_SUB: {
                Value b = pop(); Value a = pop();
                if (a.type == ValueType::INT && b.type == ValueType::INT)
                    push(Value::makeInt(a.integer - b.integer));
                else { ValueType t = promote(a.type, b.type); push(makeNum(asNum(a)-asNum(b), t)); }
                break;
            }
            case OP_MUL: {
                Value b = pop(); Value a = pop();
                if (a.type == ValueType::INT && b.type == ValueType::INT)
                    push(Value::makeInt(a.integer * b.integer));
                else { ValueType t = promote(a.type, b.type); push(makeNum(asNum(a)*asNum(b), t)); }
                break;
            }
            case OP_DIV: {
                Value b = pop(); Value a = pop();
                ValueType t = promote(a.type, b.type);
                double r = asNum(a) / asNum(b);
                push(t == ValueType::INT ? Value::makeInt((int64_t)r) : Value::makeFloat(r));
                break;
            }
            case OP_MOD: {
                Value b = pop(); Value a = pop();
                if (a.type == ValueType::INT && b.type == ValueType::INT)
                    push(Value::makeInt(a.integer % b.integer));
                else push(Value::makeFloat(fmod(asNum(a), asNum(b))));
                break;
            }
            case OP_NEG: {
                Value a = pop();
                if (a.type == ValueType::INT) push(Value::makeInt(-a.integer));
                else if (a.type == ValueType::FLOAT) push(Value::makeFloat(-a.floating));
                else runtimeError("Operand must be a number for negation");
                break;
            }
            case OP_POW: { Value b=pop(); Value a=pop(); ValueType t=promote(a.type,b.type); push(makeNum(pow(asNum(a),asNum(b)),t)); break; }

            // === Comparison ===
            case OP_EQ: { Value b=pop(); Value a=pop(); push(Value::makeBool(a.equals(b))); break; }
            case OP_NE: { Value b=pop(); Value a=pop(); push(Value::makeBool(!a.equals(b))); break; }
            case OP_LT: { Value b=pop(); Value a=pop();
                if (a.type==ValueType::INT && b.type==ValueType::INT) { push(Value::makeBool(a.integer<b.integer)); }
                else { push(Value::makeBool(asNum(a)<asNum(b))); } break; }
            case OP_GT: { Value b=pop(); Value a=pop();
                if (a.type==ValueType::INT && b.type==ValueType::INT) { push(Value::makeBool(a.integer>b.integer)); }
                else { push(Value::makeBool(asNum(a)>asNum(b))); } break; }
            case OP_LE: { Value b=pop(); Value a=pop();
                if (a.type==ValueType::INT && b.type==ValueType::INT) { push(Value::makeBool(a.integer<=b.integer)); }
                else { push(Value::makeBool(asNum(a)<=asNum(b))); } break; }
            case OP_GE: { Value b=pop(); Value a=pop();
                if (a.type==ValueType::INT && b.type==ValueType::INT) { push(Value::makeBool(a.integer>=b.integer)); }
                else { push(Value::makeBool(asNum(a)>=asNum(b))); } break; }

            // === Bitwise ===
            case OP_BIT_AND: { Value b=pop(); Value a=pop();
                if (a.type==ValueType::INT&&b.type==ValueType::INT) { push(Value::makeInt(a.integer&b.integer)); }
                else { runtimeError("Bitwise AND requires integers"); } break; }
            case OP_BIT_OR: { Value b=pop(); Value a=pop();
                if (a.type==ValueType::INT&&b.type==ValueType::INT) { push(Value::makeInt(a.integer|b.integer)); }
                else { runtimeError("Bitwise OR requires integers"); } break; }
            case OP_BIT_XOR: { Value b=pop(); Value a=pop();
                if (a.type==ValueType::INT&&b.type==ValueType::INT) { push(Value::makeInt(a.integer^b.integer)); }
                else { runtimeError("Bitwise XOR requires integers"); } break; }
            case OP_BIT_NOT: { Value a=pop();
                if (a.type==ValueType::INT) { push(Value::makeInt(~a.integer)); }
                else { runtimeError("Bitwise NOT requires integer"); } break; }
            case OP_SHL: { Value b=pop(); Value a=pop();
                if (a.type==ValueType::INT&&b.type==ValueType::INT) { push(Value::makeInt(a.integer<<b.integer)); }
                else { runtimeError("Shift left requires integers"); } break; }
            case OP_SHR: { Value b=pop(); Value a=pop();
                if (a.type==ValueType::INT&&b.type==ValueType::INT) { push(Value::makeInt(a.integer>>b.integer)); }
                else { runtimeError("Shift right requires integers"); } break; }

            // === Logical ===
            case OP_NOT: { Value a=pop(); push(Value::makeBool(!a.isTruthy())); break; }

            // === Control flow ===
            case OP_JMP:  { int16_t o=(int16_t)readShort(); frame()->pc += o; break; }
            case OP_JZ:   { int16_t o=(int16_t)readShort(); if(!pop().isTruthy()) frame()->pc+=o; break; }
            case OP_JNZ:  { int16_t o=(int16_t)readShort(); if(pop().isTruthy()) frame()->pc+=o; break; }
            case OP_LOOP: { int16_t o=(int16_t)readShort(); frame()->pc -= o; break; }

            // === Functions ===
            case OP_CALL: {
                uint8_t argCount = readByte();
                int funcIdx = (int)pop().integer;
                if (funcIdx < 0 || funcIdx >= (int)functions.size()) {
                    runtimeError("Invalid function index %d", funcIdx);
                    return RUNTIME_ERROR;
                }
                Function* callee = functions[funcIdx].get();
                if (callee->arity != (int)argCount) {
                    runtimeError("Function %s expects %d args, got %d",
                        callee->name.c_str(), callee->arity, argCount);
                    return RUNTIME_ERROR;
                }
                CallFrame cf;
                cf.function = callee;
                cf.pc = 0;
                cf.fp = (int)stack.size() - argCount;
                frames.push_back(cf);
                // Pre-allocate local variable slots (grows stack past args)
                int localEnd = cf.fp + callee->numLocals;
                while ((int)stack.size() < localEnd)
                    stack.push_back(Value::nil());
                break;
            }

            case OP_RET: {
                Value result = pop();
                int oldFp = frame()->fp;
                frames.pop_back();
                if (frames.empty()) { push(result); return OK; }
                // Restore stack to just past caller's locals
                while ((int)stack.size() > oldFp) stack.pop_back();
                push(result);
                break;
            }

            // === Objects ===
            case OP_NEW_TUPLE: { readByte(); push(Value::nil()); break; }

            // === I/O ===
            case OP_PRINT:   { peek().print(); break; }
            case OP_PRINTLN: { pop().print(); printf("\n"); break; }

            // === System ===
            case OP_HALT:
                return OK;

            default:
                runtimeError("Unknown opcode %d", instr);
                return RUNTIME_ERROR;
        }
    }
}
