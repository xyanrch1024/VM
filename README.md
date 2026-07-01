# MiniLua VM

A stack-based bytecode virtual machine for **MiniLua**, a Lua-like scripting language, written in C++17.

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Usage

```bash
./build/vm                    # REPL
./build/vm -f script.lua      # run file
./build/vm -e 'print(1+2)'    # inline code
./build/vm -b testname        # run Builder test
```

## Architecture

```
vm/                  VM core (bytecodes, interpreter, value types)
frontend/            Lexer, Parser, AST, Compiler
```

- **Lexer** — tokenizer for MiniLua syntax
- **Parser** — recursive-descent parser → AST
- **Compiler** — AST → bytecode for stack VM
- **VM** — bytecode interpreter with 40 opcodes
- **AST** — expression/statement tree with heap-allocated nodes

## Status

| Phase | Feature | Status |
|-------|---------|--------|
| VM    | Core bytecodes, chunks, value types | Done |
| VM    | Call frames, locals, recursion | Done |
| VM    | String interning | Done |
| VM    | Builder tests (13 passing) | Done |
| VM    | CMake migration | Done |
| 1     | Lexer, Parser, AST, Compiler | Compiles |
| 2     | Variables, control flow | Pending |
| 3     | Functions, closures | Pending |
| 3.5   | Coroutines | Pending |
| 4     | Tables | Pending |

## Plans

Implementation plans for upcoming features are in the [`plans/`](plans/) directory:
- **Closures** — nested functions, upvalues, closure objects
- **Coroutines** — cooperative multitasking, yield/resume
