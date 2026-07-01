# 栈式虚拟机设计文档

## 1. 总体架构

```
┌──────────────────────────────────────────────────┐
│                    VM Instance                    │
├──────────────────────────────────────────────────┤
│  Function*        当前执行的函数                   │
│  vector<Value>    操作数栈 (含局部变量区域)        │
│  vector<CallFrame> 调用栈                         │
│  vector<string>   字符串表 (intern池)              │
│  vector<Function> 函数表                          │
│  int pc           程序计数器 (当前函数内偏移)      │
│  int fp           帧指针 (当前函数栈基址)          │
└──────────────────────────────────────────────────┘
```

**架构选择: 栈式虚拟机**

栈式 vs 寄存器式对比:
- 栈式: 指令更短 (操作数隐含在栈中), 编译器后端极简, 每条指令 1-3 字节
- 寄存器式: 指令数少约 30%, 减少 push/pop 流量, 利于 JIT, 但需寄存器分配
- 本实现选择栈式: 纯解释执行场景下实现简单、Bug少、代码生成方便

## 2. 值系统 (Value)

### 类型标签

```c
enum ValueType : uint8_t {
    NIL,     // 空值
    BOOL,    // 布尔
    INT,     // 64位有符号整数
    FLOAT,   // 64位双精度浮点
    STRING,  // 字符串 (指向 VM 字符串表)
};
```

### 内存布局 (8 字节)

```
┌──────┬──────────────────────────────┐
│ type │           value               │
│ 1B   │           7B                  │
├──────┼──────────────────────────────┤
│ NIL  │  padding                      │
│ BOOL │  bool                         │
│ INT  │  int64_t                      │
│FLOAT │  double                       │
│STRING│  void* (→ string table)       │
└──────┴──────────────────────────────┘
```

### 类型转换规则

- **算术提升**: INT + FLOAT → FLOAT; INT + INT → INT
- **字符串拼接**: STRING + STRING → STRING (intern)
- **真值判断**: nil/false → false; 数字 0 → false; 空字符串 → false; 其余 true

## 3. 指令编码

### 格式

每条指令以 1 字节 opcode 开头, 后接变长操作数:

```
OP_NIL           → [opcode]                              (1B)
OP_CONSTANT      → [opcode] [index: 1B]                  (2B)
OP_CONSTANT_LONG → [opcode] [index: 2B LE]               (3B)
OP_LOAD/OP_STORE → [opcode] [slot: 1B]                   (2B)
OP_JMP/OP_JZ     → [opcode] [offset: 2B LE signed]       (3B)
OP_CALL          → [opcode] [argc: 1B]                   (2B)
OP_LOOP          → [opcode] [offset: 2B LE signed]       (3B)
```

大立即数 (>16位) 通过常量池间接引用, 指令本身保持紧凑。

### 指令集完整列表 (40条)

#### 常量压栈
| Opcode | 编码 | 操作 | 说明 |
|--------|------|------|------|
| `OP_NIL` | `00` | `push(nil)` | 压入 nil |
| `OP_TRUE` | `01` | `push(true)` | 压入 true |
| `OP_FALSE` | `02` | `push(false)` | 压入 false |
| `OP_CONSTANT` | `03` | `push(constants[idx])` | 压入常量池值 (idx < 256) |
| `OP_CONSTANT_LONG` | `04` | `push(constants[idx])` | 压入常量池值 (idx < 65536) |

#### 栈操作
| Opcode | 编码 | 操作 | 说明 |
|--------|------|------|------|
| `OP_POP` | `05` | `pop()` | 弹出栈顶 |
| `OP_DUP` | `06` | `push(top)` | 复制栈顶 |
| `OP_SWAP` | `07` | `a=pop;b=pop;push(a);push(b)` | 交换栈顶两值 |
| `OP_OVER` | `08` | `push(peek(1))` | 复制次栈顶 |
| `OP_ROT` | `09` | `a,b,c=pop;push(b);push(a);push(c)` | 轮转栈顶三值 |

#### 局部变量 (slot 由 `numLocals` 预分配)
| Opcode | 编码 | 操作 | 说明 |
|--------|------|------|------|
| `OP_LOAD` | `0A` | `push(stack[fp+slot])` | 读局部变量 |
| `OP_STORE` | `0B` | `stack[fp+slot]=top` | 写局部变量 |
| `OP_LOAD_0~3` | `0C-0F` | `push(stack[fp+0~3])` | 快速读 slot 0-3 |
| `OP_STORE_0~3` | `10-13` | `stack[fp+0~3]=top` | 快速写 slot 0-3 |

#### 算术运算
| Opcode | 编码 | 操作 | 说明 |
|--------|------|------|------|
| `OP_ADD` | `14` | `push(a+b)` | 加 (含字符串拼接) |
| `OP_SUB` | `15` | `push(a-b)` | 减 |
| `OP_MUL` | `16` | `push(a*b)` | 乘 |
| `OP_DIV` | `17` | `push(a/b)` | 除 (整数截断) |
| `OP_MOD` | `18` | `push(a%b)` | 取模 |
| `OP_NEG` | `19` | `push(-a)` | 取负 |
| `OP_POW` | `1A` | `push(a^b)` | 幂运算 |

#### 比较运算
| Opcode | 编码 | 操作 | 说明 |
|--------|------|------|------|
| `OP_EQ` | `1B` | `push(a==b)` | 相等 |
| `OP_NE` | `1C` | `push(a!=b)` | 不等 |
| `OP_LT` | `1D` | `push(a<b)` | 小于 |
| `OP_GT` | `1E` | `push(a>b)` | 大于 |
| `OP_LE` | `1F` | `push(a<=b)` | 小于等于 |
| `OP_GE` | `20` | `push(a>=b)` | 大于等于 |

#### 位运算
| Opcode | 编码 | 操作 | 说明 |
|--------|------|------|------|
| `OP_BIT_AND` | `21` | `push(a&b)` | 按位与 |
| `OP_BIT_OR` | `22` | `push(a\|b)` | 按位或 |
| `OP_BIT_XOR` | `23` | `push(a^b)` | 按位异或 |
| `OP_BIT_NOT` | `24` | `push(~a)` | 按位取反 |
| `OP_SHL` | `25` | `push(a<<b)` | 左移 |
| `OP_SHR` | `26` | `push(a>>b)` | 右移 |

#### 逻辑运算
| Opcode | 编码 | 操作 | 说明 |
|--------|------|------|------|
| `OP_NOT` | `27` | `push(!a.isTruthy())` | 逻辑非 |

#### 控制流
| Opcode | 编码 | 操作 | 说明 |
|--------|------|------|------|
| `OP_JMP` | `28` | `pc += offset` | 无条件跳转 |
| `OP_JZ` | `29` | `if(!pop()) pc+=offset` | 假则跳转 |
| `OP_JNZ` | `2A` | `if(pop()) pc+=offset` | 真则跳转 |
| `OP_LOOP` | `2B` | `pc -= offset` | 回退跳转 |

#### 函数调用
| Opcode | 编码 | 操作 | 说明 |
|--------|------|------|------|
| `OP_CALL` | `2C` | `call(funcIdx, argc)` | 调用函数 |
| `OP_RET` | `2D` | `return val` | 函数返回 |

#### 其他
| Opcode | 编码 | 操作 | 说明 |
|--------|------|------|------|
| `OP_NEW_TUPLE` | `2E` | (预留) | 创建元组 |
| `OP_PRINT` | `2F` | `print(top)` | 打印栈顶 |
| `OP_PRINTLN` | `30` | `println(pop())` | 打印并换行 |
| `OP_HALT` | `31` | `stop` | 停机 |

## 4. 执行模型

### 操作数栈

栈是 `vector<Value>`, 分为两个区域:

```
┌───────────────────┐
│   局部变量区域      │  [fp, fp+numLocals)
│   slot 0,1,...,N  │  LOAD/STORE 访问
├───────────────────┤
│   表达式栈          │  [fp+numLocals, sp]
│   临时计算值        │  PUSH/POP 操作
└───────────────────┘
```

- 函数入口时预分配 numLocals 个 nil 作为局部变量槽位
- POP 仅删除表达式栈顶, 不会影响局部变量区域
- STORE 将表达式栈顶值拷贝到局部变量槽
- LOAD 将局部变量槽值压入表达式栈

### 调用栈 (CallFrame)

```c
struct CallFrame {
    Function* function;  // 当前执行的函数
    int pc;              // 程序计数器 (函数内字节码偏移)
    int fp;              // 帧指针 (栈基址)
};
```

### 函数调用约定

```
调用前:
  stack: [...caller_frame..., arg0, arg1, ..., funcIdx]

CALL 指令:
  1. 弹出 funcIdx, 查找函数表
  2. 检查参数个数匹配
  3. 设置 fp = stack.size() - argc (指向 arg0)
  4. 预分配局部变量: push nil × numLocals
  5. 压入新 CallFrame

RET 指令:
  1. 弹出返回值
  2. 弹出 CallFrame
  3. 清理栈: 弹出到 caller 的 fp
  4. 压入返回值
```

### 解释执行主循环 (伪代码)

```
while (running) {
    instr = code[pc++];
    switch (instr) {
        case OP_CONSTANT:
            v = constants[code[pc++]];
            if (v.type == STRING) v.ptr = intern(v.ptr);
            push(v);
            break;

        case OP_ADD:
            b = pop(); a = pop();
            if (a,b are ints) push(a.int + b.int);
            else promote(a,b), push(asNum(a) + asNum(b));
            break;

        case OP_JZ:
            offset = readShort();
            if (!pop().isTruthy()) pc += offset;
            break;

        case OP_CALL:
            argc = readByte();
            funcIdx = pop().integer;
            callee = functions[funcIdx];
            fp = stack.size() - argc;
            pre_allocate_locals(callee.numLocals);
            push_frame(callee);
            break;

        case OP_RET:
            result = pop();
            old_fp = frame().fp;
            pop_frame();
            reset_stack_to(old_fp);
            push(result);
            break;

        case OP_HALT:
            return OK;
    }
}
```

## 5. 内存管理

### 字符串驻留 (String Interning)

```
VM::internString(s):
  1. 在 stringMap (hash map) 中查找 s
  2. 若存在, 返回已有指针
  3. 若不存在, 创建 new string, 加入 stringTable 和 stringMap
  4. 返回稳定指针

优点: 字符串相等性比较退化为指针比较 (O(1))
```

### 常量池字符串生命周期

```
Builder::pushStr("hello") → new string → 存入 Chunk.constants
VM::interpret:
  OP_CONSTANT 加载时:
    internString(*str_ptr) → VM 的 stringTable 拥有新副本
    原 str_ptr 仍留在 constants 中
~Chunk(): 析构时 delete 所有 constants 中的 string 指针
~VM(): 析构时释放 stringTable 中所有 string
```

## 6. 代码结构

```
vm/
├── value.hpp      Value 类型定义 + 工厂方法 + 真值/相等/打印
├── chunk.hpp      字节码序列 (Chunk), Opcode 枚举, Function 定义
├── chunk.cpp      Chunk 读写操作 + 析构 (清理字符串常量)
├── debug.hpp      反汇编接口
├── debug.cpp      反汇编实现 (每条指令的可读输出)
├── vm.hpp         VM 类定义 + CallFrame + 字符串表
├── vm.cpp         VM 解释执行主循环 + 全部指令实现
├── main.cpp       测试程序 + Builder 辅助类
├── DESIGN.md      本设计文档
└── Makefile       构建脚本 (g++ -std=c++17)
```

### 组件职责

| 文件 | 职责 | 外部依赖 |
|------|------|----------|
| `value.hpp` | 值类型系统 | 无 |
| `chunk.hpp/cpp` | 字节码序列化、常量池 | value.hpp |
| `debug.hpp/cpp` | 反汇编输出 | chunk.hpp |
| `vm.hpp/cpp` | 解释器核心 | chunk.hpp, debug.hpp |
| `main.cpp` | 测试 + Builder DSL | vm.hpp |

## 7. 编译示例

### 例 1: 1 + 2 × 3 = 7

**字节码**:
```
OP_CONSTANT  0   ; push 1
OP_CONSTANT  1   ; push 2
OP_CONSTANT  2   ; push 3
OP_MUL           ; 2 × 3 = 6
OP_ADD           ; 1 + 6 = 7
OP_PRINTLN       ; print 7
OP_HALT
```

**Builder 构建**:
```cpp
Builder b(chunk, 1);
b.pushInt(1).pushInt(2).pushInt(3).mul().add().println().halt();
```

### 例 2: 条件分支 (true ? 42 : 0)

**字节码**:
```
OP_TRUE
OP_JZ        → else_branch     ; if false goto else
OP_CONSTANT 42                  ; then: push 42
OP_JMP       → end              ; skip else
else_branch:
OP_CONSTANT 0                   ; else: push 0
end:
OP_PRINTLN                      ; print result
OP_HALT
```

**Builder 构建**:
```cpp
auto b = Builder(chunk, 1);
b.True();
int jElse = b.jz();        // 写入 JZ, 记录待回填位置
b.pushInt(42);
int jEnd = b.jmp();         // 写入 JMP, 记录待回填位置
b.patchHere(jElse);         // JZ 跳转到此处 (else 分支)
b.pushInt(0);
b.patchHere(jEnd);          // JMP 跳转到此处 (结束)
b.println().halt();
```

### 例 3: 递归阶乘 factorial(5) = 120

```
main:
  OP_CONSTANT  5
  OP_CONSTANT  funcIdx(0)   ; 函数索引
  OP_CALL      1             ; factorial(5)
  OP_PRINTLN
  OP_HALT

factorial(n):  ; numLocals=1 (参数 n 在 slot 0)
  OP_LOAD_0                  ; n
  OP_CONSTANT  1
  OP_LE                      ; n <= 1 ?
  OP_JZ        → recurse
  OP_CONSTANT  1             ; return 1
  OP_RET
recurse:
  OP_LOAD_0                  ; n
  OP_LOAD_0                  ; n
  OP_CONSTANT  1
  OP_SUB                     ; n-1
  OP_CONSTANT  funcIdx(0)
  OP_CALL      1             ; factorial(n-1)
  OP_MUL                     ; n * factorial(n-1)
  OP_RET
```

## 8. 测试验证

| 测试 | 输入 | 期望输出 | 验证特性 |
|------|------|----------|----------|
| hello | `pushStr("Hello, World!")` | `Hello, World!` | 字符串常量、I/O |
| arithmetic | `1 + 2 ∗ 3` | `7` | 算术运算、运算符优先级 |
| locals | `a=10, b=20, a+b` | `30` | 局部变量存取 |
| condition | `true ? 42 : 0` | `42` | 条件分支、跳转回填 |
| loop | `sum(1..10)` | `55` | 循环、回退跳转 |
| factorial | `factorial(5)` | `120` | 递归函数、调用栈 |
| float | `3.14 ∗ 2 + 1` | `7.28` | 浮点运算、类型提升 |
| string | `"Hello, " + "World!"` | `Hello, World!` | 字符串拼接 |
| compare | `5>3, 2==2, 7<=4` | `true, true, false` | 比较运算 |
| boolean | `!true, !false, nil` | `false, true, nil` | 逻辑非、真值 |
| stack | `dup, swap, over` | `84, 4, -5` | 栈操作指令 |
| bitwise | `&, \|, ^, ~, <<, >>` | `4, 15, 11, -6, 8, 4` | 位运算 |
| fibonacci | `fib(10)` | `55` | 双递归调用 |

## 9. 性能考虑 (后续优化方向)

- **Top-of-stack caching**: 将栈顶值缓存到寄存器, 减少内存访问
- **直接线程化 (computed goto)**: 将 switch 替换为跳转表, 减少分支预测失败
- **内联缓存 (inline caching)**: 对频繁调用的函数做内联
- **寄存器分配**: 对热点局部变量分配真实 CPU 寄存器
- **JIT 编译**: 将热点字节码编译为本地机器码
