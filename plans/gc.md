# Garbage Collector Design

## 1. Why GC

The VM currently relies entirely on RAII and `unique_ptr`. This works for `Function` objects and interned strings but cannot handle:

- **Tables** forming cyclic references (table A → table B → table A)
- **Closure + Upvalue** circularity (closure captures upvalue pointing back)
- **First-class objects** on the stack outliving their creation scope

A tracing garbage collector is needed before implementing Phase 3 (closures) and Phase 4 (tables).

---

## 2. Object Model

### 2.1 `Obj` Base

Every GC-managed heap object starts with a common header:

```cpp
enum class ObjType : uint8_t {
    STRING,    // Phase 2 (optional)
    TABLE,     // Phase 4
    CLOSURE,   // Phase 3
    UPVALUE,   // Phase 3
};

struct Obj {
    ObjType type;
    bool marked;        // GC mark bit (false = candidate for sweep)
    Obj* next;          // intrusive linked list of all GC objects
};
```

The VM maintains a singly-linked list (`gcHead`) of all allocated objects. During sweep, unmarked nodes are unlinked and freed.

### 2.2 Subclasses

```cpp
struct ObjString : Obj {
    std::string str;
};

struct ObjTable : Obj {
    // TODO: define when phase 4 starts
};

struct ObjClosure : Obj {
    Function* function;
    int upvalueCount;
    ObjUpvalue** upvalues;   // heap-allocated array of pointers
};

struct ObjUpvalue : Obj {
    Value* location;    // nullptr = closed
    Value closedValue;
};
```

---

## 3. Value Type Changes

### 3.1 New `ValueType` Entry

```cpp
enum class ValueType : uint8_t {
    NIL, BOOL, INT, FLOAT, STRING,
    OBJ,                // NEW: catch-all for GC-managed objects
};
```

All GC-managed types use a single `ValueType::OBJ` tag. The specific subtype is in `obj->type`.

### 3.2 Union Update

```cpp
struct Value {
    ValueType type;
    union {
        int64_t integer;
        double floating;
        bool boolean;
        void* ptr;       // STRING: std::string* (non-GC, RAII)
        Obj* obj;        // OBJ: GC-managed Obj*
    };

    static Value makeObj(Obj* o) { return {ValueType::OBJ, {.obj = o}}; }
    bool isObj() const { return type == ValueType::OBJ; }
};
```

`STRING` and `OBJ` are separate: strings remain RAII-managed in Phase 1.

### 3.3 `Value::print` / `Value::equals`

Extended to handle `OBJ` by switching on `obj->type`:

```cpp
case ValueType::OBJ:
    switch (obj->type) {
        case ObjType::TABLE:   printTable(...);  break;
        case ObjType::CLOSURE: printf("function %s", closure->function->name.c_str()); break;
        ...
    }
```

---

## 4. GC Algorithm: Tri-Color Mark-and-Sweep

### 4.1 Overview

```
gcAlloc(size)
  ├─ if (gcCount >= threshold) → collect()
  ├─ malloc & zero memory
  ├─ obj->next = gcHead; gcHead = obj
  ├─ gcBytes += size
  └─ return obj

collect()
  ├─ markRoots()    // trace from roots
  └─ sweep()        // free unmarked, unmark survivors
```

### 4.2 Roots

Every collection traces from:

| Root source | How it's traced |
|-------------|-----------------|
| **VM stack** | Iterate all `Value` entries; `markValue(v)` for each |
| **Call frame slots** | Stack includes frame locals — same as above |
| **Function constant pools** | `for (fn : functions) for (c : fn.chunk.constants) markValue(c)` |
| **Open upvalues** | `location` points into stack (already traced). Closed `closedValue` traced. |
| **Intern table** (Phase 2) | All `ObjString*` entries are pinned |

### 4.3 Mark Functions

```cpp
void VM::markRoots() {
    for (auto& v : stack) markValue(v);
    for (auto& fn : functions)
        for (auto& c : fn->chunk.constants)
            markValue(c);
}

void VM::markValue(Value& v) {
    if (v.type == ValueType::OBJ) markObj(v.obj);
}

void VM::markObj(Obj* obj) {
    if (!obj || obj->marked) return;
    obj->marked = true;

    switch (obj->type) {
        case ObjType::STRING:   break;                         // leaf
        case ObjType::TABLE:    /* trace entries (future) */   break;
        case ObjType::CLOSURE:
            for (int i = 0; i < closure->upvalueCount; i++)
                markObj(closure->upvalues[i]);
            break;
        case ObjType::UPVALUE:
            if (upvalue->location == nullptr) // closed
                markValue(upvalue->closedValue);
            break;
    }
}
```

### 4.4 Sweep

```cpp
void VM::sweep() {
    Obj** prev = &gcHead;
    while (*prev) {
        Obj* obj = *prev;
        if (obj->marked) {
            obj->marked = false;       // reset for next cycle
            prev = &obj->next;
        } else {
            *prev = obj->next;
            freeObj(obj);
        }
    }
}
```

### 4.5 Trigger

Allocation counter:

```cpp
static const int GC_THRESHOLD = 1000;

Obj* VM::gcAlloc(size_t size) {
    gcCount++;
    if (gcCount >= GC_THRESHOLD) {
        collect();
        gcCount = 0;
    }
    // allocate...
}
```

---

## 5. GC Allocation API

### 5.1 `gcAlloc`

```cpp
Obj* VM::gcAlloc(size_t size) {
    gcCount++;
    if (gcCount >= GC_THRESHOLD) {
        collect();
        gcCount = 0;
    }
    Obj* obj = (Obj*)malloc(size);
    obj->marked = false;
    obj->next = gcHead;
    gcHead = obj;
    gcBytes += size;
    return obj;
}
```

### 5.2 Typed Allocators

```cpp
template<typename T, typename... Args>
T* VM::gcNew(Args&&... args) {
    T* obj = (T*)gcAlloc(sizeof(T));
    new (obj) T(std::forward<Args>(args)...);
    return obj;
}
```

### 5.3 `freeObj`

```cpp
void VM::freeObj(Obj* obj) {
    gcBytes -= obj->size;  // if Obj tracks size
    switch (obj->type) {
        case ObjType::STRING:  /* ~string() runs */ break;
        case ObjType::TABLE:   /* dtor for entries */ break;
        case ObjType::CLOSURE: free(closure->upvalues); break;
        case ObjType::UPVALUE: break;
    }
    free(obj);
}
```

---

## 6. String Ownership (Phase 2)

### 6.1 Current Model (Phase 1 — Keep As-Is)

```
Chunk constants               VM intern table
┌─────────────────┐           ┌─────────────────────┐
│ str A —new—→ ptr │           │ unique_ptr<string>  │
│ str B —new—→ ptr │           │ unique_ptr<string>  │
└─────────────────┘           └─────────────────────┘
(Chunk dtor deletes ptr)      (VM dtor deletes all)
```

Strings are double-allocated: Chunk owns the original, `OP_CONSTANT` re-interns into a VM-owned copy. Equality uses pointer comparison on the interned copy.

### 6.2 GC String Model (Phase 2)

```
ObjString (GC-managed)
┌─────────────────────────────┐
│ Obj header                  │
│ std::string str (RAII)      │
└─────────────────────────────┘
GC traces: stack → OBJ → ObjString
Intern table: ObjString* (keeps alive)
```

- `OP_CONSTANT` no longer re-interns — pushes the ObjString directly
- GC traces constant pools as roots → strings survive until their Function is freed
- Intern table entries always marked → all interned strings survive until VM shutdown
- Chunk destructor no longer deletes `ptr` for STRING values

### 6.3 Allocation Path Changes (Phase 2)

| Site | Current | Phase 2 |
|------|---------|---------|
| Compiler `compileExpr(String)` | `new std::string(v)` | `vm.allocString(v)` |
| Builder `pushStr()` | `new std::string(s)` | `vm.allocString(s)` |
| Assembler `.string` directive | `new std::string(unesc)` | `internString(unesc)` |
| `Chunk::deserialize` | `new std::string()` | `internString(content)` |

### 6.4 Builder / Assembler Impact (Phase 2)

Builder currently takes `(Chunk& c, int line)`. In Phase 2 it needs a `VM&` for allocation.

All 13 test functions updated from `Builder b(func->chunk, 1)` to `Builder b(vm, func->chunk, 1)`.

---

## 7. Phase Plan

### Phase 1: GC Infrastructure

**Goal**: GC can collect tables, closures, upvalues. Strings remain RAII.

| Step | Files | Changes |
|------|-------|---------|
| 1.1 | `vm/obj.hpp` (new) | `ObjType`, `Obj`, `ObjTable`, `ObjClosure`, `ObjUpvalue` |
| 1.2 | `vm/value.hpp` | Add `OBJ` to ValueType, `Obj*` to union, `makeObj()`, `isObj()` |
| 1.3 | `vm/vm.hpp` | Add `gcHead`, `gcCount`, `gcBytes`, mark/sweep declarations |
| 1.4 | `vm/vm.cpp` | Implement `gcAlloc`, `collect`, `markRoots`, `markValue`, `markObj`, `sweep`, `freeObj` |
| 1.5 | `CMakeLists.txt` | No change needed (obj.hpp is header-only) |

### Phase 2: GC Strings (Optional)

| Step | Files | Changes |
|------|-------|---------|
| 2.1 | `vm/obj.hpp` | Add `ObjString` |
| 2.2 | `vm/vm.cpp` | `allocString()`, rewrite `internString()` to use ObjString |
| 2.3 | `vm/chunk.cpp` | Chunk dtor no longer deletes STRING values |
| 2.4 | `frontend/compiler.cpp` | String constants use `vm.allocString()` |
| 2.5 | `main.cpp` | Builder takes `VM&`, `pushStr` uses `vm.allocString()` |
| 2.6 | `vm/mbs.cpp` | Assembler strings use `internString` |
| 2.7 | `vm/vm.cpp` | OP_CONSTANT no longer re-interns |

---

## 8. Memory Management Summary

| Type | Phase 1 | Phase 2 |
|------|---------|---------|
| `Function` | `unique_ptr` (VM) | unchanged |
| `Chunk` (code/constants) | member of Function | unchanged |
| `Value::STRING (std::string*)` | Chunk dtor or `unique_ptr` (intern) | GC-managed `ObjString` |
| `Value::OBJ (Obj*)` | GC-managed | unchanged |
| `CallFrame` | `vector` inline | unchanged |
| `VM::stack` | `vector` inline | unchanged |

---

## 9. Interaction with Other Plans

### Closures (`plans/closures.md`)
- `Closure` → `ObjClosure` (GC-managed)
- `Upvalue` → `ObjUpvalue` (GC-managed, needed for sharing)
- GC marks closures → marks upvalues → marks closed values
- Open upvalue `location` stays valid because stack is a root

### Coroutines (`plans/coroutines.md`)
- Each coroutine has its own stack → additional roots for GC
- Coroutine objects themselves can be GC-managed

### Tables (future, after closures)
- `ObjTable` is GC-managed
- Table keys/values are `Value` → `markValue` on each entry
- Cycles (table → table) handled naturally

---

## 10. Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| **GC pause time** | Small heap → fast collections. Future: incremental GC. |
| **Stack reallocation** | `vector<Value>` may move → open upvalue `location` stale. Mitigation: use slot indices, regenerate location on closure creation. |
| **Double-free on Chunk dtor** | In Phase 2, Chunk must NOT delete STRING values. Critical to remove delete in `~Chunk` and `clear()`. |
| **Builder API breakage** | Adding `VM&` to Builder breaks all 13 tests. Worth doing once in Phase 2. |
