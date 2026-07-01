# AGENTS.md — MiniLua VM

## Build & Run

```bash
cmake -S . -B build
cmake --build build
./build/vm                    # REPL
./build/vm -f script.lua      # run file
./build/vm -e 'print(1+2)'    # inline code
./build/vm -c in.lua -o out.mbc  # compile source to bytecode file
./build/vm -x out.mbc         # execute bytecode file
./build/vm -S in.lua -o out.mbs  # compile source to .mbs text format
./build/vm -a in.mbs -o out.mbc  # assemble .mbs text to .mbc binary
./build/vm -d file.mbc [-o out.mbs]  # disassemble .mbc to .mbs text
./build/vm -b testname        # run single Builder test (13 available)
./build/vm hello              # same as -b hello (legacy)
VM_DEBUG=1 ./build/vm -e '...'  # disassemble bytecode
```

Debug build adds `-Wall -Wextra -g -O0`. Release adds `-O2 -DNDEBUG`.
Optional `VM_DEBUG_TRACE` CMake option enables per-instruction stack/opcode tracing.

## Code Organization

```
vm/                  → VM core
  chunk.{hpp,cpp}    → Opcodes, Chunk (bytecode buffer), Function
  value.hpp          → Tagged union Value type (NIL/BOOL/INT/FLOAT/STRING)
  vm.{hpp,cpp}       → Interpreter (dispatch loop), string interning
  debug.{hpp,cpp}    → Disassembler
frontend/            → Language frontend
  ast.{hpp,cpp}      → AST node types (Expr, Stmt) — heap-allocated
  lexer.{hpp,cpp}    → Tokenizer
  parser.{hpp,cpp}   → Recursive-descent parser → AST
  compiler.{hpp,cpp} → AST → bytecode
main.cpp             → Entry point, Builder class (inline), 13 tests, REPL, CLI
plans/               → Implementation plans (closures, coroutines)
DESIGN.md            → Architecture docs (in Chinese)
LANG_DESIGN.md       → Language spec (in Chinese)
SYNTAX.md            → Syntax reference (English)
```

## Architecture & Data Flow

```
source → Lexer → tokens → Parser → AST → Compiler → bytecode (Chunk) → VM → output
```

Two testing modes:
- **Builder tests** (main.cpp:13-419): Construct bytecode directly via `Builder` fluent API, bypassing the frontend. All 13 currently passing.
- **Source tests**: Parse + compile MiniLua source, then interpret. Used by CLI (`-f`, `-e`, REPL).

## Key Gotchas

- **LSP clangd errors are false positives.** The LSP can't resolve `#include "vm.hpp"` from `frontend/` files because the include path is set via CMake's `target_include_directories`. The project compiles cleanly — ignore LSP diagnostics. They manifest as `pp_file_not_found`, `unknown_typename`, `undeclared_var_use` cascading errors.

- **`print()` is a compiler intrinsic**, not a real function. The compiler recognizes `print(...)` by name and emits `OP_PRINTLN`. `print` is tracked as a fake local at slot 0 with special handling. This means `print` can't be used as a value or passed around.

- **All variables must be declared with `local`**. There are no global variables. Referencing an undeclared variable is a compile-time error.

- **Single return values only.** Unlike Lua's multiple returns, MiniLua only supports single return values.

- **String ownership is tricky.** Two ownership models coexist:
  - Chunk destructor deletes `STRING` values in `constants` (strings are `new`-allocated there).
  - VM's `stringTable` holds `unique_ptr<string>` for interned strings (used at runtime). When a string constant is loaded by the VM, it re-interns the pointer via `internString()` and replaces the Value's ptr.
  - In `main.cpp`'s Builder tests, strings are created with `new std::string(...)` and their ownership is transferred to the Chunk.

- **Control flow uses signed 16-bit offsets.** Jump offsets are patched at compile time. `patchJump()` and `patchLoop()` handle forward and backward jumps. See `Builder::patchHere()` and `Builder::patchLoop()` in `main.cpp:96-110`.

- **Slot 0-3 have dedicated fast opcodes** (`OP_LOAD_0`..`OP_STORE_3`). The compiler and Builder both use them when slots fit in range. Slots beyond 3 use generic `OP_LOAD`/`OP_STORE` with a 1-byte operand.

- **Lua precedence levels** are 9 levels (from `()` at 1 to `or` at 9), right-associative for `^` and unary ops. See `SYNTAX.md` for the full table.

- **`for`, `function name(...)`, `break`** are reserved in lexer/parser but **not implemented**. The AST and compiler don't handle them.

- **`local x = 10` always emits `OP_STORE` + `OP_POP`** even when the store is immediately followed by a pop. No peephole optimization exists.

- **String equality** uses pointer comparison (`ptr == other.ptr`), relying on interning guaranteeing at most one copy per string content.

## Value System

Tagged union in 8 bytes: 1 byte type tag (`ValueType`), 7 bytes union payload.

Truthiness: `nil`/`false`/`0`/`0.0`/`""` → false; everything else → true.

Arithmetic promotion: `INT + FLOAT → FLOAT`; `INT + INT → INT`. String concatenation uses `OP_ADD` when both operands are STRING.

## Testing

13 Builder tests in `main.cpp:142-418`:
`hello`, `arithmetic`, `locals`, `condition`, `loop`, `factorial`, `float`, `string`, `compare`, `boolean`, `stack`, `bitwise`, `fibonacci`

Run all: `./build/vm -b all` or `./build/vm -b` (no testname runs all).
Run one: `./build/vm -b factorial`.

No test framework — tests are raw C functions with manual printf-assertion.

CMake registers a single `smoke` test: `ctest` just runs the binary with no arguments (which runs all tests).

## Upcoming Features

Plans in `plans/`:
- **GC** — tri-color mark-and-sweep collector (Phase 1: infrastructure, Phase 2: GC strings). Required for closures/tables.
- **Closures** — nested functions, upvalues, closure objects. Compiler already has `Upvalue` struct, `addUpvalue()`, `resolveUpvalue()`.
- **Coroutines** — cooperative multitasking, yield/resume.
