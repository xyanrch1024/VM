# kai 与 Lua 语法特性对比

kai 是基于 Lua 5.1 语法的精简方言, 运行在自研栈式字节码 VM 上。设计围绕 **实现简单 > 功能完备**, 保留核心语义 (闭包、词法作用域、一等函数) 的同时移除复杂特性。

## 1. 语法兼容

| 特性 | Lua | kai | 说明 |
|------|-----|-----|------|
| 局部变量 `local x = 1` | ✓ | ✓ | 一致 |
| 多重赋值 `local a, b = 1, 2` | ✓ | ✓ | kai 用 nil 填充缺失 |
| 块语句 `do ... end` | ✓ | ✗ | 保留字未实现 |
| `if/then/elseif/else/end` | ✓ | ✓ | 一致 |
| `while/do/end` | ✓ | ✓ | 一致 |
| `repeat/until` | ✓ | ✓ | 一致 |
| 数值 `for` (`for i=1,10 do`) | ✓ | ✗ 保留 | 语法可解析，不生成代码 |
| 泛型 `for` (`for k,v in`) | ✓ | ✗ 保留 | 保留字未实现 |
| `function f() end` | ✓ 创建全局 | ✓ 语法糖 | 等价 `f = function() end`，`f` 需为已有局部变量 |
| `local function f() end` | ✓ | ✓ 语法糖 | 等价 `local f = function() end`，支持递归 |
| `return` | ✓ | ✓ | kai 仅单返回值 |
| `break` | ✓ | ✗ 保留 | 保留字未实现 |
| `goto` | ✓ | ✗ | 不支持 |
| `;` 分隔符 | ✓ 可选 | ✓ 可选 | 一致 |

## 2. 数据结构

| 类型 | Lua | kai | 说明 |
|------|-----|-----|------|
| `nil` | ✓ | ✓ | |
| `boolean` | ✓ | ✓ | `true` / `false` |
| `number` | float (所有版本) / int+float (5.3+) | int + float | kai 区分整型和浮点, 字面量含小数点则存为 float |
| `string` | ✓ | ✓ | 不可变, 自动驻留 (interned) |
| `table` | ✓ 全能 (数组+字典+对象) | ✓ 简化版 | kai 用 `vector<Entry>` 线性查找, 无元表/`__index` |
| `function` | ✓ 一等函数 | ✓ 一等函数 | 支持闭包和上值 (upvalue) |
| `userdata` / `thread` | ✓ | ✗ | — |
| 元表 (metatable) | ✓ 完整 | ✗ | 无运算符重载/继承 |

## 3. 运算符

| 类别 | Lua | kai | 说明 |
|------|-----|-----|------|
| 算术 `+ - * / % ^` | ✓ | ✓ | `^` 通过 `pow()` 实现 |
| `//` 整除 | Lua 5.3+ | ✗ | — |
| 位运算 `& \| ~ << >>` | Lua 5.3+ | ✓ | kai 提前支持 |
| 关系 `== ~= < > <= >=` | ✓ | ✓ | 类型不同则不等 |
| 逻辑 `and or not` | ✓ | ✓ | 短路求值 |
| 拼接 `..` | ✓ | ✓ | 编译为 `OP_ADD`, 两字符串操作数时做拼接 |
| 取长度 `#` | ✓ | ✗ | 保留字未实现 |
| 取反 `-` (一元) | ✓ | ✓ | |
| 不等于 `~=` | ✓ | ✓ | |

## 4. 真值判定

| 值 | Lua | kai |
|----|-----|-----|
| `nil` | false | false |
| `false` | false | false |
| `0` | **true** | **false** |
| `0.0` | true | false |
| `""` (空串) | true | **false** |
| 其他 | true | true |

**不同** : kai 中 `0`、`0.0`、`""` 均为假, 与 Lua 不同。

## 5. 内置函数

| 函数 | Lua | kai | 说明 |
|------|-----|-----|------|
| `print(...)` | ✓ | ✓ | 编译器内联为 `OP_PRINTLN` |
| `type(v)` | ✓ | ✓ | 编译器内联为 `OP_TYPE` |
| `read()` | ✗ | ✓ | **kai 特有**, 从 stdin 读取一行, EOF 返回 nil |
| `tostring(v)` | ✓ | ✗ | — |
| `tonumber(v)` | ✓ | ✗ | — |
| `pcall()` / `xpcall()` | ✓ | ✗ | — |
| `error()` / `assert()` | ✓ | ✗ | — |
| `ipairs()` / `pairs()` | ✓ | ✗ | — |
| `math.*` | ✓ 完整 | ✗ | 无标准库 |
| `string.*` | ✓ 完整 | ✗ | 无标准库 |
| `table.*` | ✓ 完整 | ✗ | 无标准库 |
| `io.*` / `os.*` | ✓ | ✗ | 无标准库 |

## 6. 技术架构对比

| 方面 | Lua (PUC-Rio) | kai |
|------|--------------|-----|
| 实现语言 | ANSI C | C++17 |
| 虚拟机 | 寄存器式 (基于分析 & 代码生成) | **栈式** (直接线程化 dispatch) |
| 编译流程 | 源码 → 字节码 (一次扫描) | 源码 → **AST** → 字节码 |
| 闭包 | upvalue + 开放上值链表 (同享) | 同 Lua, 使用 `captureUpvalue()` / `closeUpvalues()` |
| GC | 增量标记-清扫 (白色/灰色/黑色) | 三色标记-清扫 (`markRoots` → `sweep`) |
| 协程 | 完整支持 (不对称协程) | 计划中 |
| 尾调用优化 | ✓ 全支持 | ✗ |
| 字符串 | 短字符串自动驻留, 长字符串哈希 | 全量驻留 (`internString`) |
| 包/模块 | `require` / `module` | ✗ 无 |
| 错误处理 | `pcall` / `xpcall` / `error` | 运行时直接 `runtimeError()` + 栈回溯 |
| 外部接口 |完整的 C API (`lua_State*`)| ✗ 无 |

## 7. kai 字节码特色

kai 实现了 40+ 操作码, 部分来自 Lua 但大部分独立设计:

| 类别 | Opcode 数量 | 说明 |
|------|------------|------|
| 常量 | 4 | `OP_NIL`/`OP_TRUE`/`OP_FALSE`/`OP_CONSTANT[ _LONG]` |
| 栈操作 | 5 | `POP`/`DUP`/`SWAP`/`OVER`/`ROT` |
| 局部变量 | 12 | `LOAD/STORE` 1-字节 + `LOAD_[0-3]`/`STORE_[0-3]` 快捷 |
| 上值 | 2 | `GET_UPVALUE`/`SET_UPVALUE` (闭包访问外层变量) |
| 算术/比较 | 16+ | `ADD`/`SUB`/`MUL`/`DIV`/`MOD`/`NEG`/`POW` + 6 比较 + 5 位运算 |
| 控制流 | 4 | `JMP`/`JZ`/`JNZ`/`LOOP` (16-bit signed offset) |
| 函数 | 4 | `CALL`/`RET`/`CLOSURE`/`HALT` |
| 表 | 3 | `NEW_TABLE`/`TABLE_GET`/`TABLE_SET` |
| I/O | 3 | `READ`/`PRINT`/`PRINTLN` |
| 类型 | 1 | `TYPE` |

总计: 46 操作码 (含快捷 variants)。

## 8. 设计哲学对比

| 维度 | Lua | kai |
|------|-----|-----|
| 目标 | 通用嵌入式脚本语言 | **教学性 VM 实现** |
| 核心权衡 | 可移植性 + 性能 | **实现简单性** |
| 功能取舍 | 保留所有主流特性 | 仅保留核心语义 (闭包 + 表) |
| 用户群体 | 游戏/嵌入式/配置 | 编译器 & VM 学习者 |
| 标准库 | 完整 (17 个库) | 零标准库 |

## 9. 快速对照表

```
特性               Lua       kai
────────────────────────────────
局部变量            ✓         ✓
全局变量            ✓ 默认    ✗ 不存在
函数定义            ✓         ✓ (需先声明)
闭包 + upvalue     ✓         ✓
递归               ✓         ✓
多返回值            ✓         ✗ 仅单值
table              ✓ 全能    ✓ 简化版
元表               ✓         ✗
协程               ✓         计划中
位运算             5.3+      ✓
0 为假             ✗         ✓
空串为假            ✗         ✓
标准库             完整      无
C API              ✓         ✗
```
