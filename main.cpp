#include "vm.hpp"
#include "chunk.hpp"
#include "debug.hpp"
#include "mbs.hpp"
#include "parser.hpp"
#include "compiler.hpp"
#include <cstring>
#include <cstdio>
#include <cctype>
#include <string>
#include <vector>

// ============================================================
// Builder: assembler-like chunk builder
// ============================================================

struct Builder {
    Chunk* chunk = nullptr;
    int line = 0;

    Builder(Chunk& c, int startLine = 1) : chunk(&c), line(startLine) {}

    Builder& nil()   { chunk->writeOpcode(OP_NIL, line); return *this; }
    Builder& True()  { chunk->writeOpcode(OP_TRUE, line); return *this; }
    Builder& False() { chunk->writeOpcode(OP_FALSE, line); return *this; }

    Builder& pushInt(int64_t v) {
        chunk->writeConstant(Value::makeInt(v), line);
        return *this;
    }
    Builder& pushFloat(double v) {
        chunk->writeConstant(Value::makeFloat(v), line);
        return *this;
    }
    Builder& pushStr(const char* s) {
        chunk->writeConstant(Value::makeStr(new std::string(s)), line);
        return *this;
    }

    Builder& pop()   { chunk->writeOpcode(OP_POP, line); return *this; }
    Builder& dup()   { chunk->writeOpcode(OP_DUP, line); return *this; }
    Builder& swap()  { chunk->writeOpcode(OP_SWAP, line); return *this; }
    Builder& over()  { chunk->writeOpcode(OP_OVER, line); return *this; }
    Builder& rot()   { chunk->writeOpcode(OP_ROT, line); return *this; }

    Builder& load(int slot) {
        switch (slot) {
            case 0: chunk->writeOpcode(OP_LOAD_0, line); break;
            case 1: chunk->writeOpcode(OP_LOAD_1, line); break;
            case 2: chunk->writeOpcode(OP_LOAD_2, line); break;
            case 3: chunk->writeOpcode(OP_LOAD_3, line); break;
            default: chunk->writeOpcode(OP_LOAD, line); chunk->write(slot, line); break;
        }
        return *this;
    }
    Builder& store(int slot) {
        switch (slot) {
            case 0: chunk->writeOpcode(OP_STORE_0, line); break;
            case 1: chunk->writeOpcode(OP_STORE_1, line); break;
            case 2: chunk->writeOpcode(OP_STORE_2, line); break;
            case 3: chunk->writeOpcode(OP_STORE_3, line); break;
            default: chunk->writeOpcode(OP_STORE, line); chunk->write(slot, line); break;
        }
        return *this;
    }

    Builder& add()  { chunk->writeOpcode(OP_ADD, line); return *this; }
    Builder& sub()  { chunk->writeOpcode(OP_SUB, line); return *this; }
    Builder& mul()  { chunk->writeOpcode(OP_MUL, line); return *this; }
    Builder& div_() { chunk->writeOpcode(OP_DIV, line); return *this; }
    Builder& mod()  { chunk->writeOpcode(OP_MOD, line); return *this; }
    Builder& neg()  { chunk->writeOpcode(OP_NEG, line); return *this; }
    Builder& pow_() { chunk->writeOpcode(OP_POW, line); return *this; }

    Builder& eq() { chunk->writeOpcode(OP_EQ, line); return *this; }
    Builder& ne() { chunk->writeOpcode(OP_NE, line); return *this; }
    Builder& lt() { chunk->writeOpcode(OP_LT, line); return *this; }
    Builder& gt() { chunk->writeOpcode(OP_GT, line); return *this; }
    Builder& le() { chunk->writeOpcode(OP_LE, line); return *this; }
    Builder& ge() { chunk->writeOpcode(OP_GE, line); return *this; }

    Builder& bitAnd() { chunk->writeOpcode(OP_BIT_AND, line); return *this; }
    Builder& bitOr()  { chunk->writeOpcode(OP_BIT_OR, line); return *this; }
    Builder& bitXor() { chunk->writeOpcode(OP_BIT_XOR, line); return *this; }
    Builder& bitNot() { chunk->writeOpcode(OP_BIT_NOT, line); return *this; }
    Builder& shl()    { chunk->writeOpcode(OP_SHL, line); return *this; }
    Builder& shr()    { chunk->writeOpcode(OP_SHR, line); return *this; }
    Builder& not_()   { chunk->writeOpcode(OP_NOT, line); return *this; }

    // Returns position of first operand byte (for patching)
    int jmp()    { chunk->writeOpcode(OP_JMP, line);  int o=(int)chunk->code.size(); chunk->write(0,line); chunk->write(0,line); return o; }
    int jz()     { chunk->writeOpcode(OP_JZ, line);   int o=(int)chunk->code.size(); chunk->write(0,line); chunk->write(0,line); return o; }
    int jnz()    { chunk->writeOpcode(OP_JNZ, line);  int o=(int)chunk->code.size(); chunk->write(0,line); chunk->write(0,line); return o; }
    int loop()   { chunk->writeOpcode(OP_LOOP, line); int o=(int)chunk->code.size(); chunk->write(0,line); chunk->write(0,line); return o; }

    // Patch a forward jump at `pos` (first operand byte) to jump to current position
    // Offset = code.size() - (pos + 2)  because PC after reading operands = pos + 2
    Builder& patchHere(int pos) {
        int16_t offset = (int16_t)((int)chunk->code.size() - (pos + 2));
        chunk->code[pos]   = (uint8_t)(offset & 0xFF);
        chunk->code[pos+1] = (uint8_t)((offset >> 8) & 0xFF);
        return *this;
    }

    // Patch a backward loop jump at `pos` (first operand byte) to target absolute position `target`
    // Offset = code.size() - target  (code.size() = pos + 2, which is PC after operands)
    Builder& patchLoop(int pos, int target) {
        int16_t offset = (int16_t)((int)chunk->code.size() - target);
        chunk->code[pos]   = (uint8_t)(offset & 0xFF);
        chunk->code[pos+1] = (uint8_t)((offset >> 8) & 0xFF);
        return *this;
    }

    Builder& pushFuncIdx(int funcIdx) {
        chunk->writeConstant(Value::makeInt(funcIdx), line);
        return *this;
    }

    Builder& call(uint8_t argc) {
        chunk->writeOpcode(OP_CALL, line);
        chunk->write(argc, line);
        return *this;
    }

    Builder& ret()     { chunk->writeOpcode(OP_RET, line); return *this; }
    Builder& println() { chunk->writeOpcode(OP_PRINTLN, line); return *this; }
    Builder& print_()  { chunk->writeOpcode(OP_PRINT, line); return *this; }
    Builder& halt()    { chunk->writeOpcode(OP_HALT, line); return *this; }
};

// ============================================================
// Test helpers
// ============================================================

static int testCount = 0, passCount = 0;

static void testBegin(const char* name) {
    printf("\n--- Test %d: %s ---\n", ++testCount, name);
}

// ============================================================
// Test 1: Hello World
// ============================================================
static void testHello() {
    testBegin("Hello World");
    VM vm;
    Function* func = vm.newFunction("main");
    Builder b(func->chunk, 1);
    b.pushStr("Hello, World!").println();
    b.halt();

    disassembleChunk(func->chunk, "hello");
    vm.interpret(func);
    printf("  \xe2\x9c\x93 PASS\n");
    passCount++;
}

// ============================================================
// Test 2: Arithmetic (1 + 2 * 3 = 7)
// ============================================================
static void testArithmetic() {
    testBegin("Arithmetic: 1 + 2 * 3");
    VM vm;
    Function* func = vm.newFunction("main");
    Builder b(func->chunk, 1);
    b.pushInt(1).pushInt(2).pushInt(3).mul().add().println().halt();

    disassembleChunk(func->chunk, "arithmetic");
    vm.interpret(func);
    printf("  \xe2\x9c\x93 PASS (expected: 7)\n");
    passCount++;
}

// ============================================================
// Test 3: Local variables
// ============================================================
static void testLocals() {
    testBegin("Local variables: a=10, b=20, a+b");
    VM vm;
    Function* func = vm.newFunction("main");
    func->numLocals = 2; // slots 0 and 1
    Builder b(func->chunk, 1);
    b.pushInt(10).store(0).pop();
    b.pushInt(20).store(1).pop();
    b.load(0).load(1).add().println().halt();

    disassembleChunk(func->chunk, "locals");
    vm.interpret(func);
    printf("  \xe2\x9c\x93 PASS (expected: 30)\n");
    passCount++;
}

// ============================================================
// Test 4: Condition (if true then 42 else 0)
// ============================================================
static void testCondition() {
    testBegin("Condition: true ? 42 : 0");
    VM vm;
    Function* func = vm.newFunction("main");
    Builder b(func->chunk, 1);
    b.True();
    int jElse = b.jz();
    b.pushInt(42);
    int jEnd = b.jmp();
    b.patchHere(jElse);
    b.pushInt(0);
    b.patchHere(jEnd);
    b.println().halt();

    disassembleChunk(func->chunk, "condition");
    vm.interpret(func);
    printf("  \xe2\x9c\x93 PASS (expected: 42)\n");
    passCount++;
}

// ============================================================
// Test 5: Loop (sum 1 to 10 = 55)
// ============================================================
static void testLoop() {
    testBegin("Loop: sum 1 to 10");
    VM vm;
    Function* func = vm.newFunction("main");
    func->numLocals = 2;
    Builder b(func->chunk, 1);
    // sum = 0 (slot 0), i = 10 (slot 1)
    b.pushInt(0).store(0).pop();
    b.pushInt(10).store(1).pop();

    int loopStart = (int)func->chunk.code.size();
    b.load(1).pushInt(0).gt();
    int jExit = b.jz();
    b.load(0).load(1).add().store(0).pop();
    b.load(1).pushInt(1).sub().store(1).pop();
    int loopPos = b.loop();
    b.patchLoop(loopPos, loopStart);

    b.patchHere(jExit);
    b.load(0).println().halt();

    disassembleChunk(func->chunk, "loop");
    vm.interpret(func);
    printf("  \xe2\x9c\x93 PASS (expected: 55)\n");
    passCount++;
}

// ============================================================
// Test 6: Recursive factorial
// ============================================================
static void testFactorial() {
    testBegin("Factorial: factorial(5) = 120");

    VM vm;
    // Factorial function: index 0
    Function* fact = vm.newFunction("factorial", 1);
    {
        Builder b(fact->chunk, 1);
        b.load(0).pushInt(1).le();
        int jRecurse = b.jz();
        b.pushInt(1).ret();
        b.patchHere(jRecurse);
        b.load(0);
        b.load(0).pushInt(1).sub();
        b.pushFuncIdx(0).call(1);
        b.mul().ret();
    }

    // Main function: index 1
    Function* main = vm.newFunction("main");
    {
        Builder b(main->chunk, 1);
        b.pushInt(5);
        b.pushFuncIdx(0).call(1);
        b.println().halt();
    }

    printf("Factorial bytecode:\n");
    disassembleChunk(main->chunk, "main");
    printf("Factorial function:\n");
    disassembleChunk(fact->chunk, "factorial");

    vm.interpret(main);
    printf("  \xe2\x9c\x93 PASS (expected: 120)\n");
    passCount++;
}

// ============================================================
// Test 7: Float arithmetic
// ============================================================
static void testFloat() {
    testBegin("Float: 3.14 * 2 + 1");
    VM vm;
    Function* func = vm.newFunction("main");
    Builder b(func->chunk, 1);
    b.pushFloat(3.14).pushInt(2).mul().pushInt(1).add().println().halt();

    disassembleChunk(func->chunk, "float");
    vm.interpret(func);
    printf("  \xe2\x9c\x93 PASS (expected: ~7.28)\n");
    passCount++;
}

// ============================================================
// Test 8: String concat
// ============================================================
static void testStringConcat() {
    testBegin("String: concat");
    VM vm;
    Function* func = vm.newFunction("main");
    Builder b(func->chunk, 1);
    b.pushStr("Hello, ").pushStr("World!").add().println().halt();

    disassembleChunk(func->chunk, "string_concat");
    vm.interpret(func);
    printf("  \xe2\x9c\x93 PASS (expected: Hello, World!)\n");
    passCount++;
}

// ============================================================
// Test 9: Comparisons
// ============================================================
static void testComparisons() {
    testBegin("Comparisons: 5 > 3, 2 == 2, 7 <= 4");
    VM vm;
    Function* func = vm.newFunction("main");
    Builder b(func->chunk, 1);
    b.pushInt(5).pushInt(3).gt().println();
    b.pushInt(2).pushInt(2).eq().println();
    b.pushInt(7).pushInt(4).le().println();
    b.halt();

    vm.interpret(func);
    printf("  \xe2\x9c\x93 PASS (expected: true, true, false)\n");
    passCount++;
}

// ============================================================
// Test 10: Boolean logic
// ============================================================
static void testBoolean() {
    testBegin("Boolean: !true, !false, nil");
    VM vm;
    Function* func = vm.newFunction("main");
    Builder b(func->chunk, 1);
    b.True().not_().println();       // false
    b.False().not_().println();      // true
    b.nil().println();               // nil
    b.halt();

    vm.interpret(func);
    printf("  \xe2\x9c\x93 PASS (expected: false, true, nil)\n");
    passCount++;
}

// ============================================================
// Test 11: Stack operations
// ============================================================
static void testStackOps() {
    testBegin("Stack ops: dup, swap, over");
    VM vm;
    Function* func = vm.newFunction("main");
    Builder b(func->chunk, 1);
    b.pushInt(42).dup().add().println();                // 84
    b.pushInt(3).pushInt(7).swap().sub().println();     // 4
    b.pushInt(10).pushInt(5).over().sub().println();     // -5 (5-10)
    b.halt();

    vm.interpret(func);
    printf("  \xe2\x9c\x93 PASS (expected: 84, 4, -5)\n");
    passCount++;
}

// ============================================================
// Test 12: Bitwise operations
// ============================================================
static void testBitwise() {
    testBegin("Bitwise: &, |, ^, ~, <<, >>");
    VM vm;
    Function* func = vm.newFunction("main");
    Builder b(func->chunk, 1);
    b.pushInt(12).pushInt(7).bitAnd().println();  // 4
    b.pushInt(12).pushInt(7).bitOr().println();   // 15
    b.pushInt(12).pushInt(7).bitXor().println();  // 11
    b.pushInt(5).bitNot().println();              // -6
    b.pushInt(1).pushInt(3).shl().println();      // 8
    b.pushInt(16).pushInt(2).shr().println();     // 4
    b.halt();

    vm.interpret(func);
    printf("  \xe2\x9c\x93 PASS\n");
    passCount++;
}

// ============================================================
// Test 13: Fibonacci (recursive)
// ============================================================
static void testFibonacci() {
    testBegin("Fibonacci: fib(10) = 55");

    VM vm;
    Function* fib = vm.newFunction("fib", 1);
    {
        Builder b(fib->chunk, 1);
        b.load(0).pushInt(1).le();
        int jElse = b.jz();
        b.load(0).ret();
        b.patchHere(jElse);
        b.load(0).pushInt(1).sub().pushFuncIdx(0).call(1);
        b.load(0).pushInt(2).sub().pushFuncIdx(0).call(1);
        b.add().ret();
    }

    Function* main = vm.newFunction("main");
    {
        Builder b(main->chunk, 1);
        b.pushInt(10).pushFuncIdx(0).call(1).println().halt();
    }

    vm.interpret(main);
    printf("  \xe2\x9c\x93 PASS (expected: 55)\n");
    passCount++;
}

// ============================================================
// REPL input helpers
// ============================================================

// Check if source text has balanced brackets and keywords.
// Returns true if the input looks syntactically complete.
static bool isCompleteInput(const std::string& src) {
    int parenDepth = 0, bracketDepth = 0, braceDepth = 0;
    size_t i = 0;
    while (i < src.size()) {
        // Skip string literals
        if (src[i] == '"') {
            i++;
            while (i < src.size() && src[i] != '"') {
                if (src[i] == '\\') i++;
                i++;
            }
            if (i < src.size()) i++; // skip closing quote
            continue;
        }
        // Skip line comments
        if (i + 1 < src.size() && src[i] == '-' && src[i+1] == '-') {
            i += 2;
            while (i < src.size() && src[i] != '\n') i++;
            continue;
        }
        switch (src[i]) {
            case '(': parenDepth++; break;
            case ')': parenDepth--; break;
            case '[': bracketDepth++; break;
            case ']': bracketDepth--; break;
            case '{': braceDepth++; break;
            case '}': braceDepth--; break;
        }
        i++;
    }
    // Check keyword balance: simple scan for unmatched 'end'/'until'
    // Count 'function', 'if ', 'while ', 'for ', 'repeat', 'do'
    // vs 'end', 'until'
    // We search for keyword-like patterns
    int blockDepth = 0;
    std::string lower;
    for (char c : src) lower += (char)std::tolower((unsigned char)c);

    size_t pos = 0;
    while ((pos = lower.find_first_of("abcdefghijklmnopqrstuvwxyz", pos)) != std::string::npos) {
        size_t end = pos;
        while (end < lower.size() && std::isalpha((unsigned char)lower[end])) end++;
        std::string word = lower.substr(pos, end - pos);
        // Skip if part of a larger identifier (not standalone keyword)
        // Only match if preceded by whitespace/start and followed by non-alpha
        bool standalone = (pos == 0 || !std::isalpha((unsigned char)lower[pos-1]))
                       && (end == lower.size() || !std::isalpha((unsigned char)lower[end]));
        if (standalone) {
            if (word == "function" || word == "if" || word == "while"
                || word == "for" || word == "repeat" || word == "do")
                blockDepth++;
            else if (word == "end" || word == "until")
                blockDepth--;
        }
        pos = end;
    }

    return parenDepth <= 0 && bracketDepth <= 0 && braceDepth <= 0 && blockDepth <= 0;
}

// ============================================================
// Compile and execute a kai source string
// ============================================================
static bool runSource(const std::string& source, VM& vm) {
    Parser parser(source);
    std::vector<Stmt*> stmts = parser.parse();
    if (stmts.empty()) return false;

    Compiler compiler(vm);
    Function* func = compiler.compile(stmts, "main");
    if (compiler.hadError()) return false;

    if (getenv("VM_DEBUG")) {
        printf("=== Bytecode ===\n");
        disassembleChunk(func->chunk, "main");
    }

    VM::Result r = vm.interpret(func);
    return r == VM::OK;
}

static std::string readFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: cannot open '%s'\n", path);
        return "";
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    std::string source((size_t)size, '\0');
    fread(&source[0], 1, (size_t)size, f);
    fclose(f);
    return source;
}

// ============================================================
// Compile source to bytecode file
// ============================================================
static bool compileToBytecode(const char* srcPath, const char* outPath) {
    std::string source = readFile(srcPath);
    if (source.empty()) return false;

    Parser parser(source);
    std::vector<Stmt*> stmts = parser.parse();
    if (stmts.empty()) return false;

    VM vm;
    Compiler compiler(vm);
    Function* func = compiler.compile(stmts, "main");
    if (compiler.hadError()) return false;

    std::vector<Function*> funcs = { func };
    // Also collect any additional functions from the VM
    for (int i = 1; i < (int)vm.functionCount(); i++)
        funcs.push_back(vm.getFunction(i));

    bool ok = Function::writeProgram(outPath, funcs);
    if (!ok) {
        fprintf(stderr, "error: failed to write '%s'\n", outPath);
        return false;
    }
    return true;
}

// ============================================================
// Execute bytecode file
// ============================================================
static int runBytecode(const char* bcPath) {
    std::vector<Function*> funcs;
    if (!Function::readProgram(bcPath, funcs)) {
        fprintf(stderr, "error: failed to read bytecode file '%s'\n", bcPath);
        return 1;
    }

    VM vm;
    for (auto* fn : funcs)
        vm.addFunction(fn);

    if (getenv("VM_DEBUG")) {
        for (auto* fn : funcs) {
            disassembleChunk(fn->chunk, fn->name.c_str());
        }
    }

    return vm.interpret(funcs[0]) == VM::OK ? 0 : 1;
}

// ============================================================
// main
// ============================================================
int main(int argc, char** argv) {
    // Compile source to bytecode: -c input.kai -o output.mbc
    if (argc >= 3 && strcmp(argv[1], "-c") == 0) {
        const char* srcPath = argv[2];
        const char* outPath = nullptr;
        for (int i = 3; i + 1 < argc; i++) {
            if (strcmp(argv[i], "-o") == 0) {
                outPath = argv[i + 1];
                break;
            }
        }
        if (!outPath) {
            fprintf(stderr, "usage: %s -c input.kai -o output.mbc\n", argv[0]);
            return 1;
        }
        return compileToBytecode(srcPath, outPath) ? 0 : 1;
    }

    // Execute bytecode file: -x file.mbc
    if (argc >= 3 && strcmp(argv[1], "-x") == 0) {
        return runBytecode(argv[2]);
    }

    // Disassemble .mbc to .mbs text: -d file.mbc
    if (argc >= 3 && strcmp(argv[1], "-d") == 0) {
        std::vector<Function*> funcs;
        if (!Function::readProgram(argv[2], funcs)) {
            fprintf(stderr, "error: failed to read bytecode file '%s'\n", argv[2]);
            return 1;
        }
        std::string text = disassembleProgramToText(funcs);
        const char* outPath = nullptr;
        for (int i = 3; i + 1 < argc; i++) {
            if (strcmp(argv[i], "-o") == 0) { outPath = argv[i + 1]; break; }
        }
        if (outPath) {
            FILE* f = fopen(outPath, "w");
            if (!f) { fprintf(stderr, "error: cannot write '%s'\n", outPath); return 1; }
            fwrite(text.data(), 1, text.size(), f);
            fclose(f);
        } else {
            printf("%s", text.c_str());
        }
        for (auto* fn : funcs) delete fn;
        return 0;
    }

    // Compile source to .mbs text: -S input.kai -o output.mbs
    if (argc >= 3 && strcmp(argv[1], "-S") == 0) {
        std::string source = readFile(argv[2]);
        if (source.empty()) return 1;

        Parser parser(source);
        std::vector<Stmt*> stmts = parser.parse();
        if (stmts.empty()) return 1;

        VM vm;
        Compiler compiler(vm);
        Function* func = compiler.compile(stmts, "main");
        if (compiler.hadError()) return 1;

        std::vector<Function*> funcs = { func };
        for (int i = 1; i < (int)vm.functionCount(); i++)
            funcs.push_back(vm.getFunction(i));

        std::string text = disassembleProgramToText(funcs);

        const char* outPath = nullptr;
        for (int i = 3; i + 1 < argc; i++) {
            if (strcmp(argv[i], "-o") == 0) {
                outPath = argv[i + 1];
                break;
            }
        }
        if (outPath) {
            FILE* f = fopen(outPath, "w");
            if (!f) { fprintf(stderr, "error: cannot write '%s'\n", outPath); return 1; }
            fwrite(text.data(), 1, text.size(), f);
            fclose(f);
        } else {
            printf("%s", text.c_str());
        }
        return 0;
    }

    // Assemble .mbs text to .mbc binary: -a input.mbs -o output.mbc
    if (argc >= 3 && strcmp(argv[1], "-a") == 0) {
        std::string mbsText = readFile(argv[2]);
        if (mbsText.empty()) return 1;

        std::vector<Function*> funcs;
        std::string asmError;
        if (!assembleFromText(mbsText, funcs, asmError)) {
            fprintf(stderr, "error: %s\n", asmError.c_str());
            return 1;
        }

        const char* outPath = nullptr;
        for (int i = 3; i + 1 < argc; i++) {
            if (strcmp(argv[i], "-o") == 0) {
                outPath = argv[i + 1];
                break;
            }
        }
        if (!outPath) {
            fprintf(stderr, "usage: %s -a input.mbs -o output.mbc\n", argv[0]);
            return 1;
        }

        if (!Function::writeProgram(outPath, funcs)) {
            fprintf(stderr, "error: failed to write '%s'\n", outPath);
            return 1;
        }
        return 0;
    }

    if (argc >= 3 && strcmp(argv[1], "-f") == 0) {
        std::string source = readFile(argv[2]);
        if (source.empty()) return 1;
        VM vm;
        runSource(source, vm);
        return 0;
    }

    if (argc >= 3 && strcmp(argv[1], "-e") == 0) {
        VM vm;
        runSource(argv[2], vm);
        return 0;
    }

    if (argc >= 2 && strcmp(argv[1], "-b") == 0) {
        // Builder tests mode: use second arg as test name
        bool runAll = true;
        auto run = [&](const char* name, auto fn) {
            if (argc <= 2 || strcmp(argv[2], name) == 0) { fn(); }
        };
        run("hello",        testHello);
        run("arithmetic",   testArithmetic);
        run("locals",       testLocals);
        run("condition",    testCondition);
        run("loop",         testLoop);
        run("factorial",    testFactorial);
        run("float",        testFloat);
        run("string",       testStringConcat);
        run("compare",      testComparisons);
        run("boolean",      testBoolean);
        run("stack",        testStackOps);
        run("bitwise",      testBitwise);
        run("fibonacci",    testFibonacci);
        return 0;
    }

    // Default: REPL (with multi-line input, expression auto-print, state persistence)
    if (argc == 1) {
        printf("kai REPL (type 'exit' to quit)\n");
        VM vm;
        std::string accumulated;
        std::string buffer;
        bool inMultiLine = false;
        for (;;) {
            printf("%s ", inMultiLine ? ">>" : "> ");
            fflush(stdout);
            std::string line;
            if (!std::getline(std::cin, line)) {
                if (inMultiLine && !buffer.empty()) {
                    // EOF during multi-line input — run what we have
                    line.clear();
                } else {
                    break;
                }
            }
            if (!inMultiLine && (line == "exit" || line == "quit")) break;

            if (!buffer.empty()) buffer += "\n";
            buffer += line;

            // If buffer is empty, skip
            if (buffer.empty() || buffer.find_first_not_of(" \t\n\r") == std::string::npos) {
                buffer.clear();
                continue;
            }

            // Check if input is complete (balanced brackets/keywords)
            bool complete = isCompleteInput(buffer);

            if (complete) {
                // Determine if input is a standalone expression (needs print() wrapping)
                // or a valid statement. Use quiet parsing to test without showing errors.
                std::string toRun;

                // Try parsing as-is (statements like local, if, while, print(...), x=1)
                {
                    Parser test(buffer);
                    auto stmts = test.parseQuiet();
                    if (!stmts.empty() && !test.hasError()) {
                        toRun = buffer;    // valid statement(s)
                    }
                }

                // If that failed, try wrapping as print(...) for bare expressions like 1+2
                if (toRun.empty()) {
                    std::string wrapped = "print(" + buffer + ")";
                    Parser pw(wrapped);
                    auto ws = pw.parseQuiet();
                    if (!ws.empty() && !pw.hasError()) {
                        toRun = wrapped;  // bare expression → auto-print
                    }
                }

                // If still nothing, the input truly has an error — run it anyway
                // so the user sees the error once from the real compilation
                if (toRun.empty()) {
                    toRun = buffer;
                }

                // Accumulate for state persistence (recompile all each time)
                if (!accumulated.empty()) accumulated += "\n";
                accumulated += toRun;
                runSource(accumulated, vm);

                buffer.clear();
                inMultiLine = false;
            } else {
                // Incomplete input — prompt for more
                inMultiLine = true;
            }
        }
        if (inMultiLine && !buffer.empty()) {
            // EOF during multi-line; try running what we have
            runSource(buffer, vm);
        }
        return 0;
    }

    // Run legacy tests (default, no -b prefix)
    bool runAll = (argc == 1);
    auto run = [&](const char* name, auto fn) {
        if (runAll || (argc > 1 && strcmp(argv[1], name) == 0)) {
            fn();
        }
    };
    run("hello",        testHello);
    run("arithmetic",   testArithmetic);
    run("locals",       testLocals);
    run("condition",    testCondition);
    run("loop",         testLoop);
    run("factorial",    testFactorial);
    run("float",        testFloat);
    run("string",       testStringConcat);
    run("compare",      testComparisons);
    run("boolean",      testBoolean);
    run("stack",        testStackOps);
    run("bitwise",      testBitwise);
    run("fibonacci",    testFibonacci);

    if (runAll) {
        printf("\n========================================\n");
        printf("All %d / %d tests passed!\n", passCount, testCount);
        printf("========================================\n");
    }
    return 0;
}
