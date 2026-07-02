# Closure & Upvalue Implementation Plan

## 1. Problem Statement

Currently, functions are called by **function index** (an integer pushed as a constant, consumed by `OP_CALL`). This works for top-level functions but breaks for:

- **Nested functions** that reference outer local variables (upvalues)
- **Anonymous functions** (`function() end` expression)
- **First-class functions** stored in variables/tables

We need proper closure support: a Closure object that bundles a Function pointer with an upvalue array.

---

## 2. Design Overview

### 2.1 New Value Types

```cpp
enum class ValueType : uint8_t {
    NIL, BOOL, INT, FLOAT, STRING,
    CLOSURE,          // new
    // COROUTINE,     // future
    // TABLE,         // future
};
```

### 2.2 Closure Object

```cpp
struct Upvalue {
    Value* location = nullptr;     // nullptr = closed
    Value closedValue;             // value after closing
};

struct Closure {
    Function* function = nullptr;
    int upvalueCount = 0;
    Upvalue upvalues[];            // flexible array member
};
```

`Closure` is a variable-size object: fixed header + flexible array of `Upvalue` entries.

### 2.3 Upvalue: Open vs Closed

| State | Meaning | Example |
|-------|---------|---------|
| **Open** | `location` points into a live frame's stack slot (the outer function hasn't returned yet) | `function f() local x; return function() print(x) end end` |
| **Closed** | `location == nullptr`, value is in `closedValue` (outer function has returned, value was copied out) | After `f()` returns, x is copied to the closure's upvalue |

---

## 3. New Opcodes

| Opcode | Encoding | Operand | Stack Effect | Description |
|--------|----------|---------|--------------|-------------|
| `OP_CLOSURE` | 1 + 2 + upvalueCount*2 | `uint16 funcIdx`, then `upvalueCount` pairs of `(isLocal:1, index:7)` | `push(Closure)` | Creates closure from function + upvalue info |
| `OP_GET_UPVALUE` | 1 + 1 | `uint8 slot` | `push(upvalues[slot])` | Read from closure's upvalue |
| `OP_SET_UPVALUE` | 1 + 1 | `uint8 slot` | `upvalues[slot] = pop()` | Write to closure's upvalue |

### 3.1 OP_CLOSURE Encoding Detail

```
Byte 0:     OP_CLOSURE opcode
Bytes 1-2:  uint16 function index (into VM's function table)
Byte 3:     isLocal_0 << 7 | index_0   (1 bit + 7 bit)
Byte 4:     isLocal_1 << 7 | index_1
...
```

Each upvalue descriptor is a single byte:
- Bit 7: `isLocal` — 1 if capturing a local of enclosing function, 0 if capturing an upvalue of enclosing function
- Bits 6-0: `index` — slot in enclosing function's locals (if isLocal) or index in enclosing function's upvalues (if !isLocal)

---

## 4. Compiler Changes

### 4.1 Variable Resolution

Current `resolveLocal` searches only the current function's locals. For closures, add `resolveUpvalue`:

```
resolveUpvalue(name):
  1. If not in enclosing function, return -1 (not found)
  2. Search enclosing function's locals → if found, addUpvalue(isLocal=true, index=slot)
  3. Search enclosing function's upvalues → if found, addUpvalue(isLocal=false, index=upvalueIdx)
  4. Recurse to next enclosing function (for deeper nesting)
  5. Return upvalue index or -1
```

Modified `resolveName(name)`:
```
resolveName(name):
  slot = resolveLocal(name)
  if slot >= 0: emit OP_LOAD <slot> / OP_STORE <slot>
  else:
    upval = resolveUpvalue(name)
    if upval >= 0: emit OP_GET_UPVALUE <upval> / OP_SET_UPVALUE <upval>
    else: error("undefined variable")
```

### 4.2 CompileState Changes

```cpp
struct CompileState {
    Function* function;
    std::vector<LocalVar> locals;
    std::vector<Upvalue> upvalues;    // ← already exists
    int scopeDepth = 0;
    int numLocals = 0;
    // NEW:
    CompileState* enclosing = nullptr;  // link to parent for upvalue resolution
};
```

### 4.3 Function Definition Compilation

When compiling `function name(params) body end` or `function() body end` (anonymous):

```
1. enterFunction() — pushes new CompileState
2. compile params → addLocal for each param
3. compile body
4. leaveFunction() — returns Function*
5. For each upvalue captured:
     emit upvalue descriptor byte (isLocal | index)
6. emit OP_CLOSURE <funcIdx>
7. If named function: OP_STORE <slot> ; OP_POP
```

### 4.4 CompileState Stack

The compiler now maintains a linked list of CompileStates:

```cpp
struct Compiler {
    CompileState* current = nullptr;  // instead of stateStack vector
    
    void enterFunction() {
        auto* state = new CompileState();
        state->enclosing = current;
        current = state;
    }
    
    Function* leaveFunction() {
        Function* fn = current->function;
        auto* old = current;
        current = current->enclosing;
        // emit closure into enclosing function
        emitClosure(fn, old->upvalues);
        delete old;
        return fn;
    }
};
```

---

## 5. VM Changes

### 5.1 Upvalue Management

The VM needs an upvalue tracking mechanism. When a closure is created, its upvalue descriptors tell the VM which slots in the enclosing frame to capture. The VM walks up the frame stack to find the matching frame and sets up each upvalue's `location` pointer.

```cpp
// In VM:
Upvalue* captureUpvalue(int frameIndex, int localSlot) {
    // Search for existing Upvalue pointing to same slot
    // (multiple closures might share the same upvalue)
    for (auto& uv : openUpvalues) {
        if (uv->location == &stack[frames[frameIndex].fp + localSlot])
            return uv;
    }
    // Create new Upvalue
    auto* uv = new Upvalue();
    uv->location = &stack[frames[frameIndex].fp + localSlot];
    openUpvalues.push_back(uv);
    return uv;
}
```

### 5.2 CloseUpvalues

When an outer function returns (`OP_RET`), any open upvalues pointing into its frame must be **closed** (value copied to `closedValue`, `location` set to nullptr):

```cpp
void closeUpvalues(int fp) {
    // Close all upvalues pointing to stack slots >= fp
    for (auto it = openUpvalues.rbegin(); it != openUpvalues.rend(); ++it) {
        if ((*it)->location >= &stack[fp]) {
            (*it)->closedValue = *(*it)->location;
            (*it)->location = nullptr;
            // Remove from open list (or mark closed)
        }
    }
}
```

### 5.3 OP_CALL Overhaul

Current `OP_CALL` pops a function **index** (integer). New `OP_CALL` pops a **Closure** object:

```cpp
case OP_CALL: {
    int argCount = readByte();
    Value calleeValue = peek(argCount);  // closure is below args
    
    if (calleeValue.type != ValueType::CLOSURE)
        runtimeError("attempt to call a non-function value");
    
    Closure* closure = (Closure*)calleeValue.ptr;
    Function* callee = closure->function;
    
    // ... arity check ...
    
    // Setup new frame
    CallFrame cf;
    cf.function = callee;
    cf.pc = 0;
    cf.fp = (int)stack.size() - argCount - 1;  // -1 for closure itself
    
    // Push closure as local 0 (so the function can access its own upvalues)
    // Actually: closure is consumed by the call; upvalues are attached to the frame
    stack[cf.fp] = calleeValue;  // Store closure at fp+0 for upvalue access
    
    // Extend stack for locals
    while ((int)stack.size() < cf.fp + callee->numLocals)
        stack.push_back(Value::nil());
    
    frames.push_back(cf);
    break;
}
```

**Important design decision**: Should the closure be stored in the frame (e.g., as local 0) or passed separately?

- Option A: The callee accesses its own closure via a reference stored in the frame. Each CallFrame gains a `Closure*` field.
- Option B: The closure is stored on the stack at `fp + 0`.

Option A is cleaner: add `Closure* closure` to `CallFrame`, the callee reads its upvalues through `frame()->closure->upvalues[slot]`.

### 5.4 OP_GET_UPVALUE / OP_SET_UPVALUE

```cpp
case OP_GET_UPVALUE: {
    uint8_t slot = readByte();
    // frame()->closure is the Closure object for the current function
    push(frame()->closure->upvalues[slot].getValue());
    break;
}

case OP_SET_UPVALUE: {
    uint8_t slot = readByte();
    frame()->closure->upvalues[slot].setValue(peek());
    break;
}
```

Where `Upvalue::getValue()` and `Upvalue::setValue()` handle the open/closed distinction:

```cpp
struct Upvalue {
    Value* location = nullptr;
    Value closedValue;
    
    Value getValue() const {
        return location ? *location : closedValue;
    }
    
    void setValue(Value v) {
        if (location) *location = v;
        else closedValue = v;
    }
};
```

### 5.5 OP_RET Changes

Before popping the frame, close all open upvalues pointing into the frame's local region:

```cpp
case OP_RET: {
    Value result = pop();
    int oldFp = frame()->fp;
    
    // Close upvalues for the returning frame
    closeUpvalues(frame()->fp);
    
    frames.pop_back();
    if (frames.empty()) { push(result); return OK; }
    while ((int)stack.size() > oldFp) stack.pop_back();
    push(result);
    break;
}
```

---

## 6. Object Lifetime & Memory Management

### 6.1 Ownership

- `Function` objects: owned by `VM::functions` (vector of unique_ptr) — unchanged.
- `Closure` objects: heap-allocated, referenced by `Value::ptr` with `ValueType::CLOSURE`.
- `Upvalue` objects: could be owned by the closure (embedded in flexible array), or separately allocated.

If `Upvalue` is embedded in `Closure::upvalues[]`, then:
- Closure owns its upvalue array
- When GC is added (future), Closure is traced and its upvalues are visited
- Open upvalues pointing to stack slots need special handling during GC

### 6.2 Garbage Collection (Future)

For now, no GC. Closures and upvalues leak when no longer reachable. Acceptable for Phase 3.

---

## 7. Edge Cases

### 7.1 Shared Upvalues

```kai
function f()
  local x = 0
  local a = function() x = x + 1 end
  local b = function() x = x + 1 end
  return a, b
end
local a, b = f()
a()  -- x = 1
b()  -- x = 2 (same x!)
```

Both closures `a` and `b` share the same `x` upvalue. When `f()` returns, `x` is closed once (copied to the single shared `Upvalue::closedValue`). The VM's `captureUpvalue()` must deduplicate: before creating a new upvalue, check if one already exists pointing to the same slot.

### 7.2 Deep Nesting

```kai
function f()
  local x = 10
  function g()
    local y = 20
    function h()
      print(x, y)  -- x is upvalue of h, y is upvalue of h
    end
    return h
  end
  return g
end
```

For `h`: `x` is an upvalue in `g`'s upvalues (not a local of `g`). Resolution chain:
- `h`'s compiler: `resolveLocal(x)` → not found
- `resolveUpvalue(x)`: check `g`'s locals → not found
- Check `g`'s upvalues → found! Add to `h`'s upvalues as `(isLocal=false, index=upvalueIdx_of_x_in_g)`

This is the "upvalue forwarding" pattern.

### 7.3 Mutating Upvalues After Return

Already handled by the close-upvalues mechanism. The `closedValue` preserves the value at the time of the outer function's return. Since we close on return (not on scope exit), the upvalue sees all mutations up to the return statement.

### 7.4 Upvalues Capturing Loop Variables

```kai
local funcs = {}
for i = 1, 3 do
  funcs[i] = function() print(i) end
end
funcs[1]()  -- prints 4 (or 1?) depending on semantics
```

In Lua 5.1, this prints 4 (i is a single variable, mutated by loop). In Lua 5.2+, each iteration gets its own i. For kai, start with Lua 5.1 semantics (single i, last value). The for-loop compiler must emit OP_CLOSURE after the loop body to capture the current i value before it changes on the next iteration.

---

## 8. Implementation Order

### Step 1: Infrastructure
- Add `ValueType::CLOSURE` to value.hpp
- Define `Closure` struct with Function* + flexible Upvalue array
- Define `Upvalue` struct with location pointer + closedValue
- Add `Closure* closure` to `CallFrame`

### Step 2: New Opcodes
- Add `OP_CLOSURE`, `OP_GET_UPVALUE`, `OP_SET_UPVALUE` to chunk.hpp
- Add disassembly for the 3 new opcodes
- Implement OP_GET_UPVALUE and OP_SET_UPVALUE in VM loop
- Implement OP_CLOSURE: read funcIdx + upvalue descriptors, create Closure, push

### Step 3: Upvalue Capture
- Implement `captureUpvalue()` with dedup logic
- Implement `closeUpvalues()` on frame return
- Modify OP_RET to call closeUpvalues before popping frame

### Step 4: OP_CALL Overhaul
- Change OP_CALL to pop a Closure instead of a function index
- Fix call convention: args on stack, closure below args
- Store closure in CallFrame for upvalue access
- Update Builder tests to use closures

### Step 5: Compiler Changes
- Add `enclosing` pointer to CompileState
- Implement `resolveUpvalue()` — search enclosing scopes
- Modify `compileExpr(NAME)` to emit OP_GET_UPVALUE when needed
- Modify `compileStmt(ASSIGN)`/`compileExpr(NAME_LVALUE)` to emit OP_SET_UPVALUE
- Implement `compileExpr(FUNCDEF)` → emit OP_CLOSURE with upvalue descriptors
- Inject closure into local slot for named functions

### Step 6: Testing
- Test nested function accessing outer locals
- Test closure persistence after outer function returns
- Test shared upvalues between multiple closures
- Test deeply nested closures
- Test recursion with closures
- Update existing factorial/fib tests

---

## 9. Potential Pitfalls

1. **Frame pointer vs stack pointer**: After OP_CALL creates a new frame, expressions push temp values above the local slots. The upvalue `location` pointers must track these positions. When the outer frame's stack grows/shrinks, existing upvalue pointers remain valid because vectors don't reallocate on every push_back (but they CAN reallocate on growth, invalidating pointers!).

   **Mitigation**: Use indices instead of pointers for upvalue locations (e.g., `int stackSlot = fp + localSlot`). Update the slot index when the frame's stack range is known. Or, reserve sufficient stack capacity upfront to avoid reallocation.

2. **Stack reallocation**: `std::vector::push_back` can reallocate the internal buffer, invalidating all `Value*` pointers including upvalue `location` pointers. 

   **Mitigation**: Either:
   - Reserve a large initial capacity (`stack.reserve(1024)`)
   - Use stack slot indices (int) instead of pointers in Upvalue
   - Or use a custom stack with stable memory addresses

   Using **stack slot indices** (`int stackSlot`) instead of raw pointers is the safest approach:
   ```
   struct Upvalue {
       int stackSlot = -1;     // index into VM::stack, -1 = closed
       Value closedValue;
       
       Value getValue(VM& vm) const {
           return stackSlot >= 0 ? vm.stack[stackSlot] : closedValue;
       }
       void setValue(VM& vm, Value v) {
           if (stackSlot >= 0) vm.stack[stackSlot] = v;
           else closedValue = v;
       }
   };
   ```

3. **Multiple return values**: Not an issue since kai uses single return values.

4. **Circular references**: Closures capturing themselves (e.g., recursive closures stored in upvalues). Won't cause issues in a non-GC world (just leaks).

---

## 10. Summary of Changes by File

| File | Changes |
|------|---------|
| `value.hpp` | Add `ValueType::CLOSURE`; define `Closure`, `Upvalue` structs |
| `chunk.hpp` | Add `OP_CLOSURE`, `OP_GET_UPVALUE`, `OP_SET_UPVALUE` to enum |
| `debug.cpp` | Add disassembly for 3 new opcodes |
| `vm.hpp` | Add `Closure* closure` to `CallFrame`; add `captureUpvalue`, `closeUpvalues` methods |
| `vm.cpp` | Implement new opcodes; overhaul `OP_CALL`; modify `OP_RET`; add upvalue capture/close |
| `compiler.hpp` | Add `enclosing` to `CompileState`; change state stack to linked list |
| `compiler.cpp` | Implement `resolveUpvalue`; modify name resolution; add function definition compilation |
| `main.cpp` | Update Builder tests to use closures; add closure-specific tests |
