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
    COROUTINE,        // new
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
| `RUNNING` | Currently executing (its own `run()` loop is active) |
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
    CoroutineState state;
    std::vector<Value> stack;          // private stack
    std::vector<CallFrame> frames;     // private call frames
    // Current execution position:
    int baseFramePc = 0;               // PC where this coroutine is paused
    
    // For resumption:
    Value resumeArg;                   // value passed by resume() as yield() return
};
```

**Key design decision**: Each coroutine gets its own stack and frame set. This is the simplest correct approach. When a coroutine yields, its entire stack is preserved. When resumed, it continues executing on its saved stack. The main VM's stack/frames remain untouched while a coroutine runs.

### 2.4 API Surface

```lua
-- Create coroutine
local co = coroutine.create(function()
  print("hello from coroutine")
  coroutine.yield(42)
  print("resumed!")
  return "done"
end)

-- Returns: status, result1, result2, ...
local ok, val = coroutine.resume(co)
print(val)  -- 42

local ok, val2 = coroutine.resume(co)
print(val2)  -- "done"

print(coroutine.status(co))  -- "dead"
```

### 2.5 Built-in Functions

| Function | Type | Description |
|----------|------|-------------|
| `coroutine.create(f)` | Builtin closure | Creates a coroutine from function `f` |
| `coroutine.resume(co, ...)` | Builtin closure | Resumes coroutine `co`; returns (ok, values...) |
| `coroutine.yield(...)` | Builtin closure | Suspends current coroutine; returns values to `resume` caller |
| `coroutine.status(co)` | Builtin closure | Returns "suspended", "running", or "dead" |

---

## 3. Implementation Strategy

### 3.1 State Machine

The VM's execution loop is a state machine. Coroutines are implemented by swapping the active stack/frames:

```
Main VM:    [instructions...] → OP_RESUME → [coroutine runs] → OP_YIELD → back to main
```

When executing inside a coroutine:
- VM uses the coroutine's `stack` and `frames` vectors instead of its own
- `OP_YIELD` copies the coroutine's return values to the caller's stack, saves the coroutine state, and swaps back to the caller's stack/frames

### 3.2 Implementation Approaches

#### Approach A: Single VM loop, context switch via variables

The VM has a pointer to "currently active" stack/frames. By default it points to the main VM's vectors. When a coroutine runs, swap the pointers.

```cpp
class VM {
    std::vector<Value>* currentStack = &mainStack;
    std::vector<CallFrame>* currentFrames = &mainFrames;
    std::vector<Value> mainStack;
    std::vector<CallFrame> mainFrames;
    
    // ...
};
```

All opcodes access `currentStack` and `currentFrames` instead of the member variables directly.

#### Approach B: Nested interpret() calls

`coroutine.resume(co)` calls `co->interpret()` which runs the coroutine's own `run()` loop. When the coroutine yields, `interpret()` returns.

This reuses the existing `interpret(Function*)` pattern but duplicates the execution loop.

**Decision**: Use Approach A. It minimizes code duplication and keeps the execution loop in one place.

### 3.3 OP_YIELD

```
Syntax:  coroutine.yield(value)  → becomes OP_YIELD
Stack:   pop(value_to_return_to_resumer), push values for resume()
```

Implementation:

```cpp
case OP_YIELD: {
    // 1. Read the number of values to pass to resume() (arg)
    uint8_t yieldArgCount = readByte();  // number of values to return to the resumer
    
    // 2. Save current execution state (PC points to next instruction)
    frame()->pc -= 1;  // rewind to OP_YIELD (so resume re-executes it)
    // Actually, we need to save PC after OP_YIELD, and on resume
    // we skip past it and continue. Let's reconsider.
    
    // 3. Pop yield values from stack (these become resume() return values)
    // ... 
    
    // 4. Switch to resumer's stack/frames
    swapStack(&resumerStack, &resumerFrames);
    
    // 5. Push return values for resume()
    for each yieldArg:
        resumerStack.push_back(yieldArg);
    // Or maybe success status first
    resumerStack.push_back(Value::makeBool(true));  // success
    // push yield values...
    
    break;
}
```

**Refined design**: On yield, we need to:
1. Save the current PC so resume knows where to continue
2. Save the coroutine's full stack/frames state
3. Switch to the resumer's context
4. Push yield values to the resumer's stack

On resume:
1. Save the resumer's current PC/state
2. Switch to the coroutine's context
3. Restore PC (the instruction after the yield)
4. Push resume arguments as yield() return values to the coroutine's stack

**PC Saving**: Each `CallFrame` has a `pc`. For the coroutine, we save the entire `frames` vector (which includes the current PC). On resume, we restore it and continue.

Actually, we don't need to save individual PCs. The coroutine's own `frames` vector IS its execution state. When we context-switch, we swap the entire `frames` vector. The PC is in each CallFrame.

### 3.4 OP_RESUME

```
Syntax:  coroutine.resume(co, ...)
Stack before: co, args...
Stack after: true/false, values...
```

Implementation:

```cpp
case OP_RESUME: {
    // 1. Pop coroutine value
    Value coVal = pop();  
    // Hmm, but args are between co and top. Need to access co below args.
    
    // 2. Verify it's a coroutine
    if (coVal.type != ValueType::COROUTINE)
        runtimeError("bad argument #1 to 'resume' (expected coroutine)");
    
    Coroutine* co = (Coroutine*)coVal.ptr;
    
    // 3. Switch to coroutine context
    saveCurrentContext();   // save main's stack/frames/pc
    restoreCoroutineContext(co);  // restore coroutine's saved state
    
    // 4. Push resume args to coroutine's stack (these become yield() return values)
    for each arg:
        co->stack.push_back(arg);
    
    // 5. The coroutine's PC should point to the instruction after its last OP_YIELD
    // Continue execution...
    
    break;
}
```

---

## 4. OP_YIELD / OP_RESUME Detailed Design

### 4.1 Calling Convention

Consider:
```lua
function consumer()
    while true do
        local val = coroutine.yield()  -- receives value from resume
        if val == nil then break end
        print(val)
    end
end

local co = coroutine.create(consumer)
coroutine.resume(co)           -- starts coroutine
coroutine.resume(co, "hello")  -- resumes with "hello", yield() returns "hello"
coroutine.resume(co)           -- resumes with nil, break exits loop
coroutine.resume(co)           -- returns false, "dead coroutine"
```

The convention:
- `coroutine.resume(co, v1, v2, ...)`:
  1. Returns `true, yield_values...` if coroutine yielded
  2. Returns `true, return_values...` if coroutine returned
  3. Returns `false, error_message` if coroutine errored

- `coroutine.yield(v1, v2, ...)`:
  - Returns the arguments passed to the next `resume()`

### 4.2 Stack Transfer on Yield/Resume

#### Yield (coroutine → resumer)

Before yield:
```
Coroutine stack: [...frame... | yield_args...]
                                       ^-- sp
```

After yield:
```
Coroutine stack: [...frame...]   (preserved)
Main stack:      [...main_frame... | true | yield_args...]
                                                      ^-- sp
```

The coroutine's stack is preserved in its own `Coroutine` object. The main VM's stack gets the yield results pushed on top.

#### Resume (resumer → coroutine)

Before resume:
```
Main stack: [...main_frame... | co | resume_args...]
                                               ^-- sp
```

The co is consumed (it's below the args). After resume switches context:
```
Coroutine stack: [...frame... | resume_args...]  
                                      ^-- sp
```

The coroutine's PC has been advanced past its last OP_YIELD. The `resume_args` become the return values of `coroutine.yield()`.

### 4.3 PC Management

This is the trickiest part. When a coroutine yields, its current `frames.back().pc` points to the next instruction to execute (the instruction after `OP_YIELD`). When resumed, execution continues from that PC.

However, `OP_YIELD` needs to read its operand (yield arg count) BEFORE saving PC. So the flow is:

1. `OP_YIELD` reads `argCount` (PC advances past the opcode + operand)
2. Pop `argCount` values from coroutine's stack (these are the yield values)
3. Save coroutine context (frames, stack, PC is now after OP_YIELD)
4. Swap to resumer's context
5. Push results to resumer's stack

On resume:
1. Save resumer's context
2. Restore coroutine's context (PC already points past OP_YIELD)
3. Push resume args to coroutine's stack (these are yield() return values)
4. Continue execution

**Wait — there's a subtlety**. The `yield()` function call inside the coroutine's source code needs to return the values from resume. The expression `coroutine.yield(val)` is compiled as:

```
compileExpr(args)                  → push "val" 
OP_CONSTANT <yield_builtin_index>  → push the yield function pointer
OP_CALL 1                          → call yield(val)
```

But `yield` is special — it must NOT set up a normal CallFrame. Instead, the `yield` function (a builtin C function) triggers the coroutine suspension.

**Alternative**: Instead of making `yield` a callable function, make it an **opcode**:

```lua
coroutine.yield(val)  →  compiles to: push(val), OP_YIELD
```

This is the approach in Lua's implementation — `yield` is not a normal function call; it's an opcode inserted by the compiler when it sees `coroutine.yield(...)`. This avoids the complication of having a call frame for yield.

### 4.4 Compiler Special-Casing for yield

Similar to how `print(...)` is special-cased to `OP_PRINTLN`:

```cpp
case ExprType::CALL: {
    auto d = (CallData*)expr->data;
    // ...
    
    if (isBuiltinCall(d->callee, "coroutine.yield")) {
        for (auto arg : d->args) compileExpr(arg);
        emitOpcode(expr->line, OP_YIELD);
        emitByte((uint8_t)d->args.size());
        break;
    }
    
    if (isBuiltinCall(d->callee, "coroutine.create")) {
        compileExpr(d->args[0]);  // push the function
        emitOpcode(expr->line, OP_CREATE_COROUTINE);
        break;
    }
}
```

This keeps `yield` as a bytecode instruction rather than a function call, eliminating the need for complex call-frame manipulation during suspension.

For `resume`, the `coroutine.resume(co, ...)` call is compiled as:

```cpp
if (isBuiltinCall(d->callee, "coroutine.resume")) {
    compileExpr(co_expr);
    for (size_t i = 1; i < d->args.size(); i++) compileExpr(d->args[i]);
    emitOpcode(expr->line, OP_RESUME);
    emitByte((uint8_t)(d->args.size() - 1));  // arg count (excluding co)
    break;
}
```

---

## 5. New Opcodes Summary

| Opcode | Encoding | Stack Effect | Description |
|--------|----------|-------------|-------------|
| `OP_CREATE_COROUTINE` | 1B | `pop(closure) → push(coroutine)` | Create coroutine from a closure |
| `OP_RESUME` | 1+1 | `pop(co), pop(args) → push(bool), push(values)` | Resume coroutine |
| `OP_YIELD` | 1+1 | `pop(args) → push(values)` | Yield from current coroutine |
| `OP_COROUTINE_STATUS` | 1B | `pop(co) → push(string)` | Get coroutine state |

---

## 6. Built-in Functions Registration

The built-in functions `coroutine.create`, `coroutine.resume`, `coroutine.yield`, `coroutine.status` are registered as native C functions (closures wrapping function pointers):

```cpp
// In VM initialization:
struct NativeFn {
    const char* name;
    NativeFunc fn;  // void(*)(VM&, int argCount)
};

// Register coroutine table as a builtin global
// coroutine = { create = <native>, resume = <native>, yield = <native>, status = <native> }
```

Or more simply, since tables aren't implemented yet: register each as a top-level local, and the compiler transforms `coroutine.create(x)` into the appropriate opcode at compile time (like `print`).

For Phase 3.5 (intermediate before full tables), use compiler intrinsics:
- `coroutine.create(f)` → `OP_CREATE_COROUTINE`
- `coroutine.resume(co, ...)` → `OP_RESUME`  
- `coroutine.yield(...)` → `OP_YIELD`
- `coroutine.status(co)` → `OP_COROUTINE_STATUS`

---

## 7. VM Context Switching

```cpp
class VM {
    // Main execution state
    std::vector<Value> mainStack;
    std::vector<CallFrame> mainFrames;
    
    // Currently active state (points to either main or a coroutine)
    std::vector<Value>* activeStack = &mainStack;
    std::vector<CallFrame>* activeFrames = &mainFrames;
    
    // Helper to switch context
    void switchTo(std::vector<Value>& stack, std::vector<CallFrame>& frames) {
        activeStack = &stack;
        activeFrames = &frames;
    }
    
    // All opcodes use aliases:
    auto& stack = *activeStack;
    auto& frames = *activeFrames;
};
```

**C++ reference members** need careful handling. Better approach: use inline accessor methods:

```cpp
Value& stackAt(int i) { return activeStack->at(i); }
void push(Value v) { activeStack->push_back(v); }
Value pop() { /* use activeStack */ }
CallFrame* frame() { return &activeFrames->back(); }
```

This keeps the opcode implementations clean and the context switch is just a pointer swap.

---

## 8. Coroutine Memory Layout

```
Coroutine {
    state: CoroutineState          // SUSPENDED | RUNNING | DEAD
    closure: Closure*              // the wrapped closure
    stack: vector<Value>           // saved stack
    frames: vector<CallFrame>      // saved call frames
}
```

- Created by `OP_CREATE_COROUTINE`: allocate `Coroutine`, set `closure`, state = `SUSPENDED`, stack and frames empty.
- On first resume: no saved state exists, so start executing the closure from pc=0 (like a normal call).
- On subsequent resumes: restore saved stack/frames, continue execution.

### 8.1 First Resume

First resume is special — the coroutine has no saved frames. We must set up the initial call:

```cpp
case OP_RESUME: {
    Value coVal = stack[...];  // the coroutine value
    Coroutine* co = (Coroutine*)coVal.ptr;
    
    if (co->state == CoroutineState::DEAD) {
        push(Value::makeBool(false));
        push(Value::makeStr(internString("cannot resume dead coroutine")));
        break;
    }
    
    if (co->state == CoroutineState::SUSPENDED && co->frames.empty()) {
        // First resume: set up initial call
        CallFrame cf;
        cf.function = co->closure->function;
        cf.pc = 0;
        cf.fp = 0;
        cf.closure = co->closure;
        
        // Copy args from resumer's stack to coroutine's stack
        co->stack.clear();
        // Reserve space for locals
        while ((int)co->stack.size() < cf.fp + cf.function->numLocals)
            co->stack.push_back(Value::nil());
        // Push args (which will be at fp+0, fp+1, ...)
        // ... (handle arg copying)
        
        co->frames.push_back(cf);
        co->state = CoroutineState::RUNNING;
    }
    
    // Switch to coroutine context
    switchContext(co);
    break;
}
```

---

## 9. Error Handling

When a coroutine errors (runtime error during its execution), the error is caught and returned as `false, error_message` from `resume()`:

```cpp
// In the execution loop:
try {
    for (;;) { ... }
} catch (const std::runtime_error& e) {
    if (inCoroutine()) {
        // Switch back to resumer
        coroutine->state = CoroutineState::DEAD;
        switchToResumer();
        push(Value::makeBool(false));
        push(Value::makeStr(internString(e.what())));
    } else {
        throw;  // re-throw to main caller
    }
}
```

---

## 10. Implementation Order

### Step 1: Coroutine Infrastructure
- Add `ValueType::COROUTINE` to value.hpp
- Define `Coroutine` struct (state, closure, saved stack, saved frames)
- Define `CoroutineState` enum (SUSPENDED, RUNNING, DEAD)

### Step 2: VM Context Switching
- Add `activeStack`, `activeFrames` pointers to VM
- Add `switchTo()` / `switchContext()` methods
- Refactor all opcode stack/frame access to use the active pointers

### Step 3: New Opcodes
- Add `OP_CREATE_COROUTINE`, `OP_RESUME`, `OP_YIELD`, `OP_COROUTINE_STATUS` to chunk.hpp
- Implement each in the VM execution loop
- Add disassembly support

### Step 4: First Resume & Yield
- Implement initial frame setup for first resume
- Implement OP_YIELD: save coroutine state, switch to resumer, push yield values
- Implement OP_RESUME: save resumer state, switch to coroutine, push args as yield return values

### Step 5: Compiler Integration
- Special-case `coroutine.create()`, `coroutine.resume()`, `coroutine.yield()`, `coroutine.status()` in compileExpr(CALL)
- Emit appropriate opcodes instead of function calls

### Step 6: Error Handling
- Wrap coroutine execution in try-catch
- On error, transition coroutine to DEAD state
- Return error message from resume()

### Step 7: Testing
- Simple yield/resume: coroutine yields once, resumer gets value
- Multiple yields: coroutine yields multiple times with different values
- Value passing: resume passes values that become yield() return values
- Return values: coroutine returns values on completion
- Dead coroutine: attempting to resume a dead coroutine returns error
- Coroutine calling another function (nested frames within coroutine)
- Nested coroutines (one coroutine resumes another)
- Error in coroutine: caught and returned as (false, msg)

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

local co = coroutine.create(makeCounter())
print(coroutine.resume(co))  -- (true, 1)
print(coroutine.resume(co))  -- (true, 2)
```

The closure's upvalues persist across yields because the coroutine's stack (including the frame that created the closure) is preserved. When the coroutine yields, the enclosing function's frame is saved in the coroutine's stack, and its upvalues remain valid (or are closed when that function returns inside the coroutine).

---

## 12. Coroutine Lifecycle Example

```
Initial state:
  Main: stack=[...], frames=[main]
  co:   stack=[], frames=[], state=SUSPENDED

1. resume(co, "hello"):
  Main: save PC; switch to co
  co:   setup initial call; state=RUNNING
  co:   stack=[closure, "hello"], frames=[fact_0]

2. Inside coroutine, val = yield():
  co:   save PC; switch to main
  co:   stack=[...], frames=[fact_0], state=SUSPENDED
  Main: restore PC; push true; push val → stack=[..., true, "hello"]

3. resume(co, "world"):
  Main: save PC; switch to co
  co:   restore PC; push "world" as yield return
  co:   state=RUNNING
  co:   stack=[..., "world"], frames=[fact_0]

4. coroutine returns:
  co:   return values on stack; state=DEAD
  co:   stack=[...], frames=[fact_0 (popped)]
  Main: restore; push true; push return_vals

5. resume(co) again:
  Main: switch to co
  co:   state=DEAD → push false, error message
  (no context switch; error returned to main)
```
