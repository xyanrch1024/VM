# Coroutine Implementation Plan

## 1. Problem Statement

Coroutines enable cooperative multitasking: a function can suspend its execution (`yield`), returning control to the caller, and later be resumed from where it left off. Unlike OS threads, coroutines are not preemptive — they only yield at explicit yield points.

This plan builds on the closure infrastructure (Phase 3). Coroutines are first-class values that wrap a closure with its own call stack.

---

## 2. Design

### 2.1 Coroutine as a Value Type

```cpp
enum class ValueType : uint8_t {
    NIL, BOOL, INT, FLOAT, STRING,
    CLOSURE,
    COROUTINE,        // new (add after CLOSURE)
};
```

### 2.2 Coroutine States

```
SUSPENDED  →  RUNNING  →  SUSPENDED
    ↑            │
    │            ↓
    └─────── DEAD
```

| State | Meaning |
|-------|---------|
| `SUSPENDED` | Initial state after creation; can be resumed |
| `RUNNING` | Currently executing |
| `DEAD` | Function returned normally or threw an error |

### 2.3 Coroutine Object

```cpp
enum class CoroutineState : uint8_t {
    SUSPENDED,
    RUNNING,
    DEAD,
};

struct Coroutine {
    Closure* closure;                  // the function to execute
    CoroutineState state = CoroutineState::SUSPENDED;
    std::vector<Value> stack;          // private stack
    std::vector<CallFrame> frames;     // private call frames
    Coroutine* resumer = nullptr;      // who to switch back to on yield (nullptr = main)
};
```

**Key design decision**: Each coroutine gets its own stack and frame set. This is the simplest correct approach. When a coroutine yields, its entire stack is preserved. When resumed, it continues executing on its saved stack. The resumer's stack/frames remain untouched while a coroutine runs.

### 2.4 API Surface

```lua
local co = coroutine.create(function()
  coroutine.yield(42)
  return "done"
end)

local ok, val = coroutine.resume(co)     -- (true, 42)
local ok, val2 = coroutine.resume(co)    -- (true, "done")
print(coroutine.status(co))              -- "dead"
```

### 2.5 Built-in Functions

| Function | Type | Description |
|----------|------|-------------|
| `coroutine.create(f)` | Compiler intrinsic | Emits `OP_CREATE_COROUTINE` |
| `coroutine.resume(co, ...)` | Compiler intrinsic | Emits `OP_RESUME` |
| `coroutine.yield(...)` | Compiler intrinsic | Emits `OP_YIELD` |
| `coroutine.status(co)` | Compiler intrinsic | Emits `OP_COROUTINE_STATUS` |

All four are handled as compiler intrinsics (like `print`), not real function calls. The compiler recognizes `coroutine.create` / `coroutine.resume` / `coroutine.yield` / `coroutine.status` in `compileExpr(CALL)` and emits the corresponding opcodes directly.

---

## 3. VM Changes

### 3.1 CallFrame Extension

Add `Closure*` field to `CallFrame` for upvalue access (already needed by closures):

```cpp
struct CallFrame {
    Function* function = nullptr;
    int pc = 0;
    int fp = 0;
    Closure* closure = nullptr;    // ← new: for upvalue access
};
```

### 3.2 VM Class Changes

```cpp
class VM {
    // Main execution state (original members)
    std::vector<Value> mainStack;
    std::vector<CallFrame> mainFrames;

    // Saved main context (for coroutine switching)
    std::vector<Value> savedMainStack;
    std::vector<CallFrame> savedMainFrames;

    // Currently active state (points to either main or a coroutine)
    std::vector<Value>* activeStack = &mainStack;
    std::vector<CallFrame>* activeFrames = &mainFrames;
    Coroutine* activeCoroutine = nullptr;  // non-null if inside a coroutine

    // Accessor helpers (opcodes use these instead of direct stack/frames access)
    Value& stackAt(int i) { return activeStack->at(i); }
    size_t stackSize() const { return activeStack->size(); }
    void push(Value v) { activeStack->push_back(v); }
    Value pop() {
        if (activeStack->empty()) { runtimeError("Stack underflow"); return Value::nil(); }
        Value v = activeStack->back();
        activeStack->pop_back();
        return v;
    }
    Value peek(int d = 0) { return (*activeStack)[activeStack->size() - 1 - d]; }
    CallFrame* frame() { return &activeFrames->back(); }

    // Context switching
    void switchTo(Coroutine* co);          // main → coroutine
    void switchToMain();                     // coroutine → main
    void switchToResumer(Coroutine* co);    // coroutine → its resumer

    // ... rest of existing methods
};
```

### 3.3 Existing Opcode Refactoring

All existing opcodes must use the accessor methods instead of direct `stack`/`frames` member access:

| Direct access | Replace with |
|---------------|-------------|
| `stack[fp + slot]` | `stackAt(fp + slot)` |
| `stack.push_back(v)` | `push(v)` |
| `stack.pop_back()` / `pop()` | `pop()` |
| `stack.back()` | `peek()` |
| `stack.size()` | `stackSize()` |
| `frames.back()` | `*frame()` |
| `frames.push_back(cf)` | `activeFrames->push_back(cf)` |
| `frames.pop_back()` | `activeFrames->pop_back()` |

This refactoring is mechanical but must be done carefully to avoid regressions.

### 3.4 OP_CALL Refactoring for Closures

`OP_CALL` currently pops a function **index** (integer). It must be changed to pop a **Closure** object:

```cpp
case OP_CALL: {
    uint8_t argCount = readByte();
    int calleeSlot = (int)stackSize() - argCount - 1;
    Value calleeVal = stackAt(calleeSlot);

    if (calleeVal.type != ValueType::CLOSURE)
        runtimeError("attempt to call a non-function value");

    Closure* closure = (Closure*)calleeVal.ptr;
    Function* fn = closure->function;

    if (fn->arity != argCount)
        runtimeError("expected %d arguments, got %d", fn->arity, argCount);

    // Remove closure value from stack (args remain)
    // Shift args down or reorg frame pointer
    // ... (see closures.md for full detail)

    CallFrame cf;
    cf.function = fn;
    cf.pc = 0;
    cf.fp = (int)stackSize() - argCount;
    cf.closure = closure;  // ← new

    while ((int)stackSize() < cf.fp + fn->numLocals)
        push(Value::nil());

    activeFrames->push_back(cf);
    break;
}
```

---

## 4. New Opcodes

### 4.1 Enum Values

Add after `OP_HALT` (currently ~45, last explicit opcode):

```cpp
enum Opcode : uint8_t {
    // ... existing opcodes ...
    OP_HALT,

    // Coroutines
    OP_CREATE_COROUTINE,   // create coroutine from closure
    OP_RESUME,             // resume a coroutine
    OP_YIELD,              // yield from current coroutine
    OP_COROUTINE_STATUS,   // get coroutine state string
};
```

### 4.2 OP_CREATE_COROUTINE

**Encoding**: `OP_CREATE_COROUTINE` (1 byte)

**Stack**: `pop(closure) → push(coroutine)`

```cpp
case OP_CREATE_COROUTINE: {
    Value v = pop();
    if (v.type != ValueType::CLOSURE)
        runtimeError("coroutine.create: expected function, got %s", valueTypeName(v.type));

    auto* co = new Coroutine();
    co->closure = (Closure*)v.ptr;
    co->state = CoroutineState::SUSPENDED;
    push(Value::makeCoroutine(co));  // new helper
    break;
}
```

`Value::makeCoroutine(Coroutine*)` stores the pointer in `Value::ptr` with `ValueType::COROUTINE`.

### 4.3 OP_RESUME

**Encoding**: `OP_RESUME <argCount:uint8>` (2 bytes)

**Stack before**: `[..., co, arg1, arg2, ...]`  (argCount args after co)
**Stack after**: `[..., true/false, value1, ...]`

```cpp
case OP_RESUME: {
    uint8_t argCount = readByte();

    // Co is below the args at stack[stackSize() - argCount - 1]
    int coSlot = (int)stackSize() - argCount - 1;
    Value coVal = stackAt(coSlot);

    if (coVal.type != ValueType::COROUTINE)
        runtimeError("bad argument #1 to 'resume' (expected coroutine)");

    Coroutine* co = (Coroutine*)coVal.ptr;

    if (co->state == CoroutineState::DEAD) {
        // Remove co and args from stack, push result
        // ... (stack cleanup)
        push(Value::makeBool(false));
        push(Value::makeStr(internString("cannot resume dead coroutine")));
        break;
    }

    // Save current context (whoever is calling resume)
    if (activeCoroutine) {
        // A coroutine is resuming another coroutine
        activeCoroutine->stack = std::move(*activeStack);
        activeCoroutine->frames = std::move(*activeFrames);
        co->resumer = activeCoroutine;
    } else {
        // Main VM is resuming a coroutine
        savedMainStack = std::move(*activeStack);
        savedMainFrames = std::move(*activeFrames);
        co->resumer = nullptr;
    }

    // Switch to target coroutine
    switchTo(co);
    co->state = CoroutineState::RUNNING;

    if (co->frames.empty()) {
        // First resume: set up initial call frame
        CallFrame cf;
        cf.function = co->closure->function;
        cf.pc = 0;
        cf.fp = 0;
        cf.closure = co->closure;

        co->stack.clear();
        // Push args (from resumer) as the function's parameters
        for (int i = 0; i < argCount; i++)
            co->stack.push_back(stackAt(coSlot + 1 + i));  // index in resumer's stack
        // Extend for locals
        while ((int)co->stack.size() < cf.fp + cf.function->numLocals)
            co->stack.push_back(Value::nil());

        co->frames.push_back(cf);
    } else {
        // Subsequent resume: push args as yield() return values
        for (int i = 0; i < argCount; i++)
            co->stack.push_back(stackAt(coSlot + 1 + i));
    }

    // Clean up resumer's stack (remove co and args)
    // Since we moved the resumer's stack into savedMainStack or
    // the resumer coroutine's stack, we just let the switchTo handle it.
    break;
}
```

### 4.4 OP_YIELD

**Encoding**: `OP_YIELD <argCount:uint8>` (2 bytes)

**Stack before**: `[..., arg1, arg2, ...]`  (yield return values)
**Stack after** (resumer's): `[..., true, arg1, arg2, ...]`

```cpp
case OP_YIELD: {
    uint8_t argCount = readByte();

    if (!activeCoroutine)
        runtimeError("cannot yield from main thread");

    Coroutine* co = activeCoroutine;

    // Pop yield values from coroutine's stack
    std::vector<Value> yieldVals;
    for (int i = 0; i < argCount; i++)
        yieldVals.insert(yieldVals.begin(), pop());
    // yieldVals are now in source order (top-of-stack was last arg)

    // Save coroutine state
    co->state = CoroutineState::SUSPENDED;
    co->stack = std::move(*activeStack);
    co->frames = std::move(*activeFrames);

    // Switch to resumer
    if (co->resumer) {
        switchTo(co->resumer);
    } else {
        switchToMain();
    }

    // Push yield values to resumer's stack
    push(Value::makeBool(true));  // success flag
    for (auto& val : yieldVals)
        push(val);

    break;
}
```

### 4.5 OP_COROUTINE_STATUS

**Encoding**: `OP_COROUTINE_STATUS` (1 byte)

**Stack**: `pop(coroutine) → push(string)`

```cpp
case OP_COROUTINE_STATUS: {
    Value v = pop();
    if (v.type != ValueType::COROUTINE)
        runtimeError("bad argument #1 to 'coroutine.status' (expected coroutine)");

    Coroutine* co = (Coroutine*)v.ptr;
    const char* status;
    switch (co->state) {
        case CoroutineState::SUSPENDED: status = "suspended"; break;
        case CoroutineState::RUNNING:   status = "running"; break;
        case CoroutineState::DEAD:      status = "dead"; break;
    }
    push(Value::makeStr(internString(status)));
    break;
}
```

---

## 5. OP_RET Coroutine Handling

When `OP_RET` executes inside a coroutine (i.e., `activeCoroutine != nullptr`), the coroutine transitions to DEAD and switches back to the resumer:

```cpp
case OP_RET: {
    Value result = pop();

    if (activeCoroutine) {
        // Coroutine function is returning
        Coroutine* co = activeCoroutine;
        co->state = CoroutineState::DEAD;

        // Switch to resumer
        if (co->resumer)
            switchTo(co->resumer);
        else
            switchToMain();

        // Push return value to resumer's stack
        push(Value::makeBool(true));  // success flag
        push(result);
        break;
    }

    // Normal return (existing logic)
    int oldFp = frame()->fp;
    activeFrames->pop_back();
    if (activeFrames->empty()) { push(result); return OK; }
    while ((int)stackSize() > oldFp) pop();
    push(result);
    break;
}
```

---

## 6. Context Switching Implementation

```cpp
void VM::switchTo(Coroutine* co) {
    activeStack = &co->stack;
    activeFrames = &co->frames;
    activeCoroutine = co;
}

void VM::switchToMain() {
    activeStack = &mainStack;
    activeFrames = &mainFrames;
    activeCoroutine = nullptr;

    // Restore saved main context
    mainStack = std::move(savedMainStack);
    mainFrames = std::move(savedMainFrames);
}

void VM::switchToResumer(Coroutine* co) {
    if (co->resumer) {
        switchTo(co->resumer);
    } else {
        switchToMain();
    }
}
```

---

## 7. Compiler Changes

### 7.1 `coroutine.create(f)` → OP_CREATE_COROUTINE

In `compileExpr(CALL)`, add:

```cpp
if (isCoroutineCall(d->callee, "create")) {
    if (d->args.size() != 1)
        error(expr->line, "coroutine.create: expected 1 argument");
    compileExpr(d->args[0]);
    emitOpcode(expr->line, OP_CREATE_COROUTINE);
    break;
}
```

Where `isCoroutineCall` checks for `callee->type == ExprType::NAME && strcmp(callee->strVal, "coroutine.create") == 0`. Or more flexibly, check for a dotted name `coroutine.create` using member access syntax when that's parsed.

### 7.2 `coroutine.resume(co, ...)` → OP_RESUME

```cpp
if (isCoroutineCall(d->callee, "resume")) {
    if (d->args.size() < 1)
        error(expr->line, "coroutine.resume: expected at least 1 argument");
    compileExpr(d->args[0]);  // the coroutine
    for (size_t i = 1; i < d->args.size(); i++)
        compileExpr(d->args[i]);  // resume args
    emitOpcode(expr->line, OP_RESUME);
    emitByte((uint8_t)(d->args.size() - 1));
    break;
}
```

### 7.3 `coroutine.yield(...)` → OP_YIELD

```cpp
if (isCoroutineCall(d->callee, "yield")) {
    for (auto arg : d->args)
        compileExpr(arg);
    emitOpcode(expr->line, OP_YIELD);
    emitByte((uint8_t)d->args.size());
    break;
}
```

### 7.4 `coroutine.status(co)` → OP_COROUTINE_STATUS

```cpp
if (isCoroutineCall(d->callee, "status")) {
    if (d->args.size() != 1)
        error(expr->line, "coroutine.status: expected 1 argument");
    compileExpr(d->args[0]);
    emitOpcode(expr->line, OP_COROUTINE_STATUS);
    break;
}
```

### 7.5 Dot-Name Parsing

The `coroutine.create` syntax requires the parser to produce a dotted name expression. If the parser doesn't support `prefixexpr '.' name` yet, add:

```cpp
// In primaryExpr or prefixExpr:
if (match(TK_DOT)) {
    Token name = consume(TK_NAME, "expected name after '.'");
    // Build INDEX expression: callee=coroutine, key="create"
    Expr* callee = previous;  // the "coroutine" part
    Expr* key = Expr::makeStr(name.line, copyStr(name.start));
    expr = Expr::makeIndex(expr->line, callee, key);
}
```

Or, simpler: register `coroutine` as a local variable (a table) and `create`/`resume`/`yield`/`status` as method-like accesses. Since MiniLua doesn't have tables yet, the simplest approach is to handle `NAME DOT NAME LPAREN ... RPAREN` as a special call syntax in the parser, producing a CALL node where the callee is a dotted name.

Actually, the simplest approach for Phase 3.5: treat `coroutine.create` as a single identifier at the lexer level? No, that's hacky.

Better approach: parse `coroutine.create` as a member access expression (`INDEX`), then detect it in the compiler by checking if the callee is an `INDEX` node with the obj being `NAME("coroutine")`. This is more general and extensible.

Alternatively, for the initial implementation, register `coroutine_create`, `coroutine_resume`, `coroutine_yield`, `coroutine_status` as flat names (using underscore instead of dot). The user writes `coroutine_create(f)` instead of `coroutine.create(f)`. This avoids the need for dotted name parsing entirely.

**Decision**: Use flat names (`coroutine_create`, `coroutine_resume`, `coroutine_yield`, `coroutine_status`) as compiler intrinsics for Phase 3.5. Support for the dotted syntax `coroutine.create` can be added when table member access is implemented in Phase 4.

---

## 8. Error Handling

### 8.1 Runtime Error Inside Coroutine

When a runtime error occurs inside a coroutine (e.g., type error, stack underflow), the error is caught and returned as `(false, error_message)` from `resume()`:

```cpp
// Modified execution loop:
Result VM::interpret(Function* func) {
    // ... setup ...

    try {
        for (;;) {
            uint8_t instr = readByte();
            switch (instr) {
                // ... all opcodes ...
            }
        }
    } catch (const std::runtime_error& e) {
        if (activeCoroutine) {
            // Coroutine errored: switch back to resumer, return error
            Coroutine* co = activeCoroutine;
            co->state = CoroutineState::DEAD;

            if (co->resumer)
                switchTo(co->resumer);
            else
                switchToMain();

            push(Value::makeBool(false));
            push(Value::makeStr(internString(e.what())));
            return OK;
        }
        throw;  // re-throw to top-level caller
    }
}
```

### 8.2 Attempting to Yield from Main

```lua
coroutine.yield(42)  -- error: cannot yield from main thread
```

Handled by the runtime error check in `OP_YIELD`: `if (!activeCoroutine) runtimeError(...)`.

### 8.3 Attempting to Resume Dead Coroutine

```lua
local co = coroutine.create(function() end)
coroutine.resume(co)   -- true
coroutine.resume(co)   -- false, "cannot resume dead coroutine"
```

Handled by the state check in `OP_RESUME`.

---

## 9. Memory Management

### 9.1 Coroutine Lifetime

`Coroutine` objects are heap-allocated by `OP_CREATE_COROUTINE` and become owned by the `Value` system (stored in `Value::ptr` with `ValueType::COROUTINE`). When no longer referenced, they leak.

For now, no GC. Acceptable for Phase 3.5. A future GC pass would trace coroutine objects and their stacks.

### 9.2 Stack Ownership

Each `Coroutine` owns its `stack` and `frames` vectors. When switching contexts, ownership is transferred via `std::move`:

```
coroutine.yield:
  co->stack = std::move(*activeStack);    // coroutine takes ownership
  switchTo(resumer);                        // activeStack now points to resumer's stack

resume:
  savedMainStack = std::move(*activeStack);  // save resumer's stack
  switchTo(co);                                // activeStack now points to co->stack
```

This ensures only one copy of the stack data exists at any time (zero-copy context switch).

---

## 10. Implementation Order

### Step 0: OP_CALL Refactoring (Closure prerequisite)
- Change `OP_CALL` from function-index to closure-based
- Add `Closure*` to `CallFrame`
- Refactor all opcodes to use accessor methods (`push()`, `pop()`, `peek()`, `stackAt()`, `frame()`)

### Step 1: Coroutine Infrastructure
- Add `ValueType::COROUTINE` to value.hpp
- Define `Coroutine` struct and `CoroutineState` enum
- Add `Value::makeCoroutine()` helper

### Step 2: VM Context Switching
- Add `activeStack`, `activeFrames`, `activeCoroutine` pointers to VM
- Add `savedMainStack`, `savedMainFrames` for main context backup
- Implement `switchTo()`, `switchToMain()`, `switchToResumer()`
- Refactor all opcode stack/frame access to use pointer-based helpers

### Step 3: New Opcodes
- Add `OP_CREATE_COROUTINE`, `OP_RESUME`, `OP_YIELD`, `OP_COROUTINE_STATUS` to chunk.hpp
- Implement each in the VM execution loop
- Modify `OP_RET` for coroutine DEAD transition
- Add try-catch for coroutine error handling
- Add disassembly support

### Step 4: Compiler Integration
- Detect `coroutine_create`, `coroutine_resume`, `coroutine_yield`, `coroutine_status` in `compileExpr(CALL)`
- Emit corresponding opcodes

### Step 5: Testing

| Test | Description |
|------|-------------|
| Create & resume | `coroutine_create(f)` then `coroutine_resume(co)` |
| Single yield | Coroutine yields once, resumer receives value |
| Multiple yields | Coroutine yields multiple times with different values |
| Value passing | Resume passes values that become yield() return values |
| Return values | Coroutine returns values on completion (true, vals) |
| Dead resume | Resuming a dead coroutine returns false, error |
| Nested calls | Coroutine calls another function internally |
| Nested coroutines | Coroutine A resumes coroutine B |
| Error in coroutine | Runtime error caught as (false, msg) |
| Yield from main | Error: cannot yield from main thread |

---

## 11. Interaction with Closures

Coroutines work naturally with closures. When a coroutine wraps a closure:

```lua
function makeCounter()
    local count = 0
    return function()
        count = count + 1
        return count
    end
end

local co = coroutine_create(makeCounter())
print(coroutine_resume(co))  -- (true, 1)
print(coroutine_resume(co))  -- (true, 2)
```

The closure's upvalues persist across yields because the coroutine's stack (including the frame that created the closure) is preserved. When the coroutine yields, the enclosing function's frame is saved in the coroutine's stack, and its upvalues remain valid (or are closed when that function returns inside the coroutine).

---

## 12. Complete Lifecycle Example

```
Initial:
  Main:  mainStack=[...], mainFrames=[main], activeStack=&mainStack
  co:    stack=[], frames=[], state=SUSPENDED, resumer=nullptr

1. resume(co, "hello"):
  → Save: savedMainStack=mainStack, savedMainFrames=mainFrames
  → switchTo(co): activeStack=&co->stack, activeFrames=&co->frames, activeCoroutine=co
  → First resume: setup frame, push "hello" as arg
  → co: stack=["hello", nil...], frames=[fact(n)], state=RUNNING

2. Inside coroutine, val = yield(42):
  → Pop 42 from co stack
  → co->stack = co's stack; co->frames = co's frames; state=SUSPENDED
  → switchToMain(): restore savedMainStack/Frames; activeCoroutine=nullptr
  → Push (true, 42) to mainStack

3. resume(co, "world"):
  → Save: savedMainStack=mainStack, savedMainFrames=mainFrames
  → switchTo(co): restore co->stack/frames
  → Push "world" as yield return value
  → Continue execution after OP_YIELD

4. Coroutine returns "done":
  → co->state = DEAD
  → if co->resumer==nullptr: switchToMain()
  → Push (true, "done") to mainStack

5. resume(co) again:
  → co->state == DEAD
  → Push (false, "cannot resume dead coroutine") to mainStack
  → No context switch
```

---

## 13. Open Questions

1. **Should `coroutine.yield()` with no args return nil?** Yes, yield() with no args returns nil (zero yield values → resume gets true).

2. **What happens if a coroutine's closure has upvalues that close during yield?** If the coroutine's function (not the closure) returns, upvalues are closed as normal. The coroutine transitions to DEAD.

3. **Should `coroutine.resume()` pass all extra args as yield return values, or just the first?** All extra args become yield's return values. MiniLua is single-return, so only the first is accessible via `val = yield()`, but the resumer can inspect via ... (if added later).

4. **Stack size limits?** No hard limit. Each coroutine's stack grows on demand. A deeply recursive coroutine may run out of memory.

5. **Can a coroutine be resumed from a different coroutine than the one that yielded it?** Yes, by passing the coroutine to another coroutine via shared state (e.g., a table). The `resumer` field tracks who should receive the yield values; if coroutine B resumes coroutine A, A's resumer is B. When A yields, control returns to B.
