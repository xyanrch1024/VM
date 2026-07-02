# 类 Lua 语言设计方案

## 1. 语言概览

**语言名**: kai

kai 是 Lua 的一个精简方言, 基于现有的栈式虚拟机。保留 Lua 的核心哲学——简约、灵活、嵌入友好。

### 设计目标

- 语法与 Lua 高度一致, 降低学习成本
- 编译到现有字节码, 最大化复用 VM 后端
- 单返回值、词法作用域、一等函数
- 动态类型: nil, boolean, number (int/float), string, table, function

### 与标准 Lua 的差异

| 特性 | Lua | kai |
|------|-----|---------|
| 返回值 | 多返回值 | 单返回值 |
| 全局变量 | 默认全局 | 仅 `local` |
| 元表 | 完整 | 暂不支持 |
| 协程 | 支持 | 暂不支持 |
| `goto` | 支持 | 不支持 |
| `for` 泛型 | 支持 | 仅数值 for |
| 标签方法 `:` | 语法糖 | 暂不支持 |

---

## 2. 语法定义

### 2.1 完整语法 (EBNF)

```
program     = { stat }

stat        = ';'
            | 'local' namelist [ '=' explist ]
            | name '=' expr
            | 'do' block 'end'
            | 'while' expr 'do' block 'end'
            | 'repeat' block 'until' expr
            | 'if' expr 'then' block
              { 'elseif' expr 'then' block }
              [ 'else' block ]
              'end'
            | 'for' name '=' expr ',' expr [ ',' expr ] 'do' block 'end'
            | 'function' name '(' [ namelist ] ')' block 'end'
            | 'return' [ expr ]
            | functioncall

block       = { stat }

namelist    = name { ',' name }
explist     = expr { ',' expr }

expr        = 'nil' | 'false' | 'true' | NUMBER | STRING
            | functiondef
            | tableconstructor
            | prefixexpr
            | expr binop expr
            | unop expr

prefixexpr  = name
            | '(' expr ')'
            | functioncall
            | prefixexpr '[' expr ']'
            | prefixexpr '.' name

functioncall = prefixexpr '(' [ explist ] ')'

functiondef = 'function' '(' [ namelist ] ')' block 'end'

tableconstructor = '{' [ fieldlist ] '}'
fieldlist   = field { ( ',' | ';' ) field } [ ',' ]
field       = expr
            | name '=' expr

binop       = '+' | '-' | '*' | '/' | '%' | '^'
            | '==' | '~=' | '<' | '>' | '<=' | '>='
            | 'and' | 'or'
            | '..'          -- 字符串拼接
unop        = '-' | 'not' | '#'
```

### 2.2 运算符优先级 (从高到低)

```
1.  ()  .  []      -- 作用域/索引
2.  #  -  not      -- 一元
3.  ^              -- 幂 (右结合)
4.  *  /  %        -- 乘法类
5.  +  -           -- 加法类
6.  ..             -- 字符串拼接
7.  <  >  <=  >=  ==  ~=
8.  and
9.  or
```

### 2.3 关键字

```
and     break     do      else      elseif
end     false     for     function  if
in      local     nil     not       or
repeat  return    then    true      until  while
```

### 2.4 词法记号

```c
enum TokenType {
    // 符号
    TK_EOF, TK_NAME, TK_NUMBER, TK_STRING,
    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_PERCENT, TK_CARET,
    TK_EQ, TK_NE, TK_LT, TK_GT, TK_LE, TK_GE,
    TK_ASSIGN, TK_LPAREN, TK_RPAREN, TK_LBRACK, TK_RBRACK,
    TK_LBRACE, TK_RBRACE, TK_DOT, TK_COMMA, TK_SEMI, TK_COLON,
    TK_DOTDOT,       // ..
    TK_ELLIPSIS,     // ...

    // 关键字
    TK_AND, TK_BREAK, TK_DO, TK_ELSE, TK_ELSEIF, TK_END,
    TK_FALSE, TK_FOR, TK_FUNCTION, TK_IF, TK_IN, TK_LOCAL,
    TK_NIL, TK_NOT, TK_OR, TK_REPEAT, TK_RETURN, TK_THEN,
    TK_TRUE, TK_UNTIL, TK_WHILE,
};
```

---

## 3. AST 定义

```cpp
// ast.hpp

enum class ExprType {
    NIL, BOOL, NUMBER, STRING,        // 字面量
    NAME,                             // 变量引用
    UNARY, BINARY,                    // 一元/二元运算
    CALL,                             // 函数调用
    INDEX,                            // 索引 t[k]
    FUNCDEF,                          // 函数定义
    TABLE,                            // 表构造器
};

struct Expr {
    ExprType type;
    int line;
    union {
        // BOOL
        bool boolVal;
        // NUMBER
        double numVal;
        // STRING / NAME
        const char* strVal;
        // UNARY
        struct { TokenType op; Expr* rhs; } unary;
        // BINARY
        struct { TokenType op; Expr* lhs; Expr* rhs; } binary;
        // CALL
        struct { Expr* callee; vector<Expr*> args; } call;
        // INDEX
        struct { Expr* obj; Expr* key; } index;
        // FUNCDEF
        struct { vector<const char*> params; Stmt* body; } func;
        // TABLE
        struct { vector<TableField> fields; } table;
    };
};

struct TableField {
    bool isIndexed;     // false: name=val; true: val
    Expr* key;          // for name=val, key is interned string
    Expr* val;
};

enum class StmtType {
    BLOCK,
    LOCAL_DECL,       // local x = expr
    ASSIGN,           // x = expr
    IF, WHILE, REPEAT, FOR,
    CALL,             // 作为语句的函数调用
    RETURN,
};

struct Stmt {
    StmtType type;
    int line;
    union {
        // BLOCK
        struct { vector<Stmt*> stmts; } block;
        // LOCAL_DECL
        struct { vector<const char*> names; vector<Expr*> inits; } localDecl;
        // ASSIGN
        struct { const char* name; Expr* value; } assign;
        // IF
        struct {
            vector<Expr*> conds;
            vector<Stmt*> bodies;   // 每个分支的 body
            Stmt* elseBody;          // else 分支 (可为 null)
        } ifStmt;
        // WHILE
        struct { Expr* cond; Stmt* body; } whileStmt;
        // REPEAT
        struct { Stmt* body; Expr* cond; } repeatStmt;
        // FOR
        struct {
            const char* var;
            Expr* start; Expr* end; Expr* step;
            Stmt* body;
        } forStmt;
        // CALL
        struct { Expr* call; } callStmt;
        // RETURN
        struct { Expr* value; } returnStmt;
    };
};
```

---

## 4. 编译策略 (AST → Bytecode)

### 4.1 编译器核心结构

```cpp
class Compiler {
    struct Local {
        const char* name;
        int depth;          // 作用域深度
        int slot;           // 局部变量槽位
    };

    struct Scope {
        int depth;
        vector<Local> locals;
    };

    vector<Scope> scopes;         // 作用域链
    Function*    function;         // 正在编译的函数
    int          numLocals = 0;   // 当前函数已分配的局部变量数
    int          scopeDepth = 0;  // 0 = 函数顶层

    // 编译入口
    void compile(Program* prog);

    // 语句编译
    void compileStmt(Stmt* stmt);
    void compileBlock(Stmt* block);

    // 表达式编译 (结果留在栈上)
    void compileExpr(Expr* expr);

    // 变量管理
    int  resolveLocal(const char* name);  // 返回 slot, -1 = 未找到
    int  addLocal(const char* name);      // 分配新 slot
    void pushScope();
    void popScope();  // 发出 POP 清理超出作用域的变量

    // 跳转辅助
    int  emitJump(Opcode op);
    void patchJump(int offset);
    int  emitLoop(int loopStart);

    // 字节码发射
    void emitByte(uint8_t byte);
    void emitOpcode(Opcode op);
    void emitConstant(Value v);
};
```

### 4.2 表达式编译规则

| Lua 源码 | 生成字节码 |
|----------|-----------|
| `nil` | `OP_NIL` |
| `true/false` | `OP_TRUE / OP_FALSE` |
| `42` | `OP_CONSTANT <int:42>` |
| `"hello"` | `OP_CONSTANT <str:"hello">` |
| `x` (local) | `OP_LOAD <slot>` |
| `-x` | `<code for x> OP_NEG` |
| `not x` | `<code for x> OP_NOT` |
| `#x` | (预留) |
| `a + b` | `<a> <b> OP_ADD` |
| `a - b` | `<a> <b> OP_SUB` |
| `a * b` | `<a> <b> OP_MUL` |
| `a / b` | `<a> <b> OP_DIV` |
| `a % b` | `<a> <b> OP_MOD` |
| `a ^ b` | `<a> <b> OP_POW` |
| `a .. b` | `<a> <b> OP_ADD` (字符串拼接复用了 ADD) |
| `a == b` | `<a> <b> OP_EQ` |
| `a ~= b` | `<a> <b> OP_NE` |
| `a < b` | `<a> <b> OP_LT` |
| `a and b` | `<a> OP_JZ →b` (短路求值) |
| `a or b` | `<a> OP_JNZ →b` (短路求值) |
| `f(args)` | `<args> <funcIdx> OP_CALL <argc>` |
| `t[k]` | `<t> <k> OP_GET_INDEX` |
| `function (p) body end` | 编译为函数, `OP_CLOSURE <idx>` |

### 4.3 短路逻辑编译

`a and b` 的编译:

```
  <code for a>
  OP_JZ → end       ; a 为假, 跳到 end (结果 = a)
  OP_POP            ; 弹出 a
  <code for b>
→ end:              ; 栈顶为结果
```

`a or b` 的编译:

```
  <code for a>
  OP_JNZ → end      ; a 为真, 跳到 end (结果 = a)
  OP_POP            ; 弹出 a
  <code for b>
→ end:              ; 栈顶为结果
```

### 4.4 语句编译规则

#### `local x = expr`

```
  <code for expr>
  OP_STORE <slot>    ; 存储到局部变量 slot
  OP_POP             ; 弹出值 (不再需要)
  // 将 x 注册到当前作用域, slot = numLocals++
```

如果是 `local x` (不初始化):

```
  OP_NIL
  OP_STORE <slot>
  OP_POP
```

#### `x = expr` (赋值)

```
  <code for expr>
  OP_STORE <slot>    ; slot 通过 resolveLocal 查找
  OP_POP
```

#### `if cond then ... elseif cond2 then ... else ... end`

```
  <code for cond>
  OP_JZ → elseif_label
  <code for then_body>
  OP_JMP → end_label
→ elseif_label:
  <code for cond2>
  OP_JZ → else_label
  <code for elseif_body>
  OP_JMP → end_label
→ else_label:
  <code for else_body>
→ end_label:
```

#### `while cond do body end`

```
→ loop_start:
  <code for cond>
  OP_JZ → end_label
  <code for body>
  OP_LOOP <loop_start>
→ end_label:
```

#### `repeat body until cond`

```
→ loop_start:
  <code for body>
  <code for cond>
  OP_JZ → loop_start   ; 条件为假则继续循环
```

#### `for var = start, end, step do body end`

```
  ; 展开为 while 循环
  local _limit = end
  local _step = step or 1
  local var = start
  while (_step > 0 and var <= _limit) or (_step < 0 and var >= _limit) do
    body
    var = var + _step
  end
```

#### `function name(params) body end`

```
  ; 编译函数体为独立 Function
  ; 发出 OP_CLOSURE <funcIdx>
  ; OP_STORE <name_slot>
  ; OP_POP
```

#### `return expr`

```
  <code for expr>
  OP_RET
```

#### `f(args)` 作为语句

```
  <code for f(args)>
  OP_POP             ; 丢弃返回值
```

### 4.5 函数与闭包

**函数编译**:
1. 创建新的 `Function` 对象
2. 在新函数中编译参数和 body
3. 记录函数的 arity 和 numLocals

**闭包支持**:
- 需要内部函数访问外部函数的局部变量 (upvalue)
- 编译器检测: 当 `resolveLocal` 在当前作用域找不到变量时, 向父函数查找
- 如果在父函数中找到, 则创建 upvalue
- 发射 `OP_CLOSURE` 指令, 携带 upvalue 信息

当前 VM 的 OP_CALL 使用函数索引调用, 不支持闭包。需要增加:

```cpp
OP_CLOSURE    // 创建闭包: 从函数 + upvalues 创建闭包对象
```

闭包对象存储:
- 函数指针
- upvalue 值列表 (指向父帧的局部变量)

---

## 5. Bytecode 扩展

需要在现有指令集上新增:

| Opcode | 编码 | 操作 | 用途 |
|--------|------|------|------|
| `OP_GET_INDEX` | `32` | `push(pop()[pop()])` | `t[k]` |
| `OP_SET_INDEX` | `33` | `pop()[pop()] = pop()` | `t[k] = v` |
| `OP_NEW_TABLE` | `34` | `push(new Table())` | `{}` |
| `OP_CLOSURE` | `35` | `push(Closure(func))` | `function() end` |
| `OP_GET_UPVALUE` | `36` | `push(upvalues[slot])` | 读取 upvalue |
| `OP_SET_UPVALUE` | `37` | `upvalues[slot] = peek()` | 写入 upvalue |

### Value 类型扩展

需要新增 `ValueType::OBJECT`, 用于表示 table 和 closure:

```cpp
enum class ValueType : uint8_t {
    NIL, BOOL, INT, FLOAT, STRING,
    TABLE, CLOSURE,  // 新增
};
```

### Table 结构

```cpp
struct Table {
    std::unordered_map<Value, Value> entries;  // key → value
    // 简单实现: 使用 Value 的相等/哈希
};
```

---

## 6. 运行时扩展

### 内置函数

kai 预置以下全局函数 (通过编译器在顶层作用域注入):

| 函数 | 实现 | 说明 |
|------|------|------|
| `print(...)` | `OP_PRINTLN` | 打印参数 |
| `type(v)` | 返回类型名字符串 | `"nil"`, `"number"`, `"string"`, `"table"`, `"function"` |
| `tostring(v)` | 转换为字符串 | 调用 Value::print |
| `tonumber(v)` | 转换为数字 | 字符串→数字, 其他→nil |

---

## 7. 文件结构

```
vm/
├── ast.hpp           ← AST 节点定义 (Expr, Stmt)
├── lexer.hpp/cpp     ← 词法分析器 (源文件 → Token 流)
├── parser.hpp/cpp    ← 语法分析器 (Token 流 → AST)
├── compiler.hpp/cpp  ← 编译器 (AST → Function 字节码)
├── builtins.cpp      ← 注入内置函数
├── value.hpp         ← 已有 (扩展 TABLE, CLOSURE 类型)
├── chunk.hpp/cpp     ← 已有 (扩展 OP_GET_INDEX 等)
├── debug.hpp/cpp     ← 已有 (扩展反汇编)
├── vm.hpp/cpp        ← 已有 (扩展 TABLE/CLOSURE 支持)
├── main.cpp          ← 改为 LSP: 读取文件/REPL
├── table.hpp/cpp     ← Table 数据结构 (可选)
├── CMakeLists.txt    ← 已有
└── DESIGN.md         ← 已有
```

---

## 8. 分阶段实现计划

### Phase 1: 词法 + 语法 + 表达式 (约 400 行)

**新增文件**: `lexer.hpp/cpp`, `parser.hpp/cpp`, `ast.hpp`, `compiler.hpp/cpp`

**实现**:
- 完整的词法分析器 (数字、字符串、标识符、关键字、运算符)
- 递归下降解析器 (完整语法)
- AST 节点定义
- 表达式编译器 (所有运算符、函数调用)
- 集成测试: `print(1 + 2 * 3)`

**测试用例**:
```kai
print(1 + 2 * 3)         -- 7
print(3.14 * 2 + 1)      -- 7.28
print(2 ^ 10)            -- 1024
print("Hello, " .. "World!")  -- Hello, World!
print(5 > 3)             -- true
print(not false)         -- true
```

### Phase 2: 控制流 + 变量 (约 300 行)

**实现**:
- 局部变量声明和赋值
- 作用域管理 (pushScope/popScope)
- if/elseif/else
- while, repeat-until
- 数值 for
- 短路逻辑 (and/or)

**测试用例**:
```kai
local a = 10
local b = 20
print(a + b)             -- 30

local x = 5
if x > 3 then
  print("big")
else
  print("small")
end                      -- big

local sum = 0
local i = 1
while i <= 10 do
  sum = sum + i
  i = i + 1
end
print(sum)               -- 55
```

### Phase 3: 函数 (约 200 行)

**实现**:
- 函数定义 `function name(params) body end`
- 函数调用
- 递归
- OP_CLOSURE + upvalue 支持

**测试用例**:
```kai
function fact(n)
  if n <= 1 then
    return 1
  end
  return n * fact(n - 1)
end
print(fact(5))           -- 120

function fib(n)
  if n <= 1 then
    return n
  end
  return fib(n - 1) + fib(n - 2)
end
print(fib(10))           -- 55
```

### Phase 4: Table (约 200 行)

**实现**:
- `ValueType::TABLE` + `Table` 结构体
- `OP_NEW_TABLE`, `OP_GET_INDEX`, `OP_SET_INDEX`
- 表构造器 `{ ... }`
- 内置函数 `type()`, `tostring()`

**测试用例**:
```kai
local t = {}
t["name"] = "Alice"
t["age"] = 30
print(t["name"])         -- Alice

local t2 = {x = 10, y = 20}
print(t2.x)              -- 10
```

---

## 9. 编译示例: 完整追踪

### 输入

```kai
function fact(n)
  if n <= 1 then return 1 end
  return n * fact(n - 1)
end
print(fact(5))
```

### 编译后字节码

**函数 main** (index=0, arity=0, numLocals=1):
```
0000  OP_CLOSURE   0    ; 创建闭包 (指向 fact 函数)
0002  OP_STORE_0         ; fact = closure
0003  OP_POP
0004  OP_CONSTANT  0     ; push 5
0006  OP_CONSTANT  1     ; push funcIdx(0)
0008  OP_CALL      1     ; fact(5)
0010  OP_PRINTLN
0011  OP_HALT
```

**函数 fact** (index=1, arity=1, numLocals=1):
```
0000  OP_LOAD_0          ; push n
0001  OP_CONSTANT  0     ; push 1
0003  OP_LE              ; n <= 1?
0004  OP_JZ        → 7   ; if false skip ret
0007  OP_CONSTANT  1     ; push 1
0009  OP_RET             ; return 1
0010  OP_LOAD_0          ; push n
0011  OP_LOAD_0          ; push n
0012  OP_CONSTANT  2     ; push 1
0014  OP_SUB             ; n - 1
0015  OP_CONSTANT  3     ; push funcIdx(1)
0017  OP_CALL      1     ; fact(n - 1)
0019  OP_MUL             ; n * fact(n-1)
0020  OP_RET             ; return
```

### 执行流程

与已有 `factorial(5)` 执行追踪完全一致。编译器替代了手写 Builder 的工作。

---

## 10. 错误处理

### 词法错误
```
[line 1] unexpected symbol near '@'
[line 3] unfinished string (eof expected)
```

### 语法错误
```
[line 5] expected 'then' near 'do'
[line 8] expected 'end' to close 'function'
```

### 编译错误
```
[line 3] variable 'x' not declared in this scope
[line 6] duplicate parameter 'n'
```

### 运行时错误
```
[line 10] attempt to call a nil value (global 'foo')
[line 12] division by zero
```

---

## 11. 测试策略

每个 Phase 使用 `.kai` 测试文件 + 预期输出:

```bash
# 运行测试
./build/vm tests/arithmetic.kai
# 期望输出: 7

# 对比测试
./build/vm tests/if.kai > /tmp/out
diff /tmp/out tests/if.expected
```

测试目录结构:
```
vm/
├── tests/
│   ├── arithmetic.kai
│   ├── variables.kai
│   ├── if.kai
│   ├── while.kai
│   ├── for.kai
│   ├── function.kai
│   ├── recursion.kai
│   ├── table.kai
│   └── ...
```
