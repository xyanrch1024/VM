# kai 语法参考

## 1. 词法

### 记号 (Token)

```
NUMBER  =  digit { digit } [ '.' digit { digit } ]
STRING  =  '"' { char } '"'  |  "'" { char } "'"
NAME    =  letter { letter | digit | '_' }
COMMENT =  '--' { char } '\n'
```

转义字符: `\\` `\"` `\'` `\n`  (字符串内)

### 关键字

```
and      break    do       else     elseif   end
false    for      function if       in       local
nil      not      or       repeat   return   then
true     until    while
```

### 运算符

```
+    -    *    /    %    ^    ..
==   ~=   <    >    <=   >=
=    (    )    [    ]    {    }
.    ..   ...  ,    ;    :
```

---

## 2. 语法 (EBNF)

### 程序

```
program     = { stat }
```

### 语句

```
stat        = ';'
            | 'local' namelist [ '=' explist ]
            | name '=' expr
            | 'while' expr 'do' block 'end'
            | 'repeat' block 'until' expr
            | 'if' expr 'then' block
              { 'elseif' expr 'then' block }
              [ 'else' block ]
              'end'
            | 'return' [ expr ]
            | functioncall

block       = { stat }
namelist    = name { ',' name }
explist     = expr { ',' expr }
```

> **状态**: `for`, `function name(...)`, `break` 已预留但**未实现**。

### 表达式

```
expr        = or_expr

or_expr     = and_expr { 'or' and_expr }
and_expr    = cmp_expr { 'and' cmp_expr }
cmp_expr    = concat_expr { ('==' | '~=' | '<' | '>' | '<=' | '>=') concat_expr }
concat_expr = add_expr { '..' add_expr }
add_expr    = mul_expr { ('+' | '-') mul_expr }
mul_expr    = pow_expr { ('*' | '/' | '%') pow_expr }
pow_expr    = unary_expr { '^' pow_expr }          (* 右结合 *)
unary_expr  = ('-' | 'not') unary_expr
            | call_expr
call_expr   = prefix_expr { '(' [ explist ] ')' }
prefix_expr = NAME
            | '(' expr ')'
            | functiondef
functiondef = 'function' '(' [ namelist ] ')' block 'end'
```

### 字面量

```
nil      → 空值
true     → 布尔真
false    → 布尔假
NUMBER   → 数值 (整数或浮点)
STRING   → 字符串
```

---

## 3. 运算符优先级 (从高到低)

| 优先级 | 运算符 | 结合性 |
|--------|--------|--------|
| 1 | `()` | —— |
| 2 | `-` `not` | 右 |
| 3 | `^` | 右 |
| 4 | `*` `/` `%` | 左 |
| 5 | `+` `-` | 左 |
| 6 | `..` | 左 |
| 7 | `==` `~=` `<` `>` `<=` `>=` | 左 |
| 8 | `and` | 左 |
| 9 | `or` | 左 |

---

## 4. 语义

### 类型

| 类型 | 取值 | 真值 |
|------|------|------|
| `nil` | `nil` | false |
| `boolean` | `true`, `false` | 值本身 |
| `number` | 64-bit int / 64-bit float | 非 0 为真 |
| `string` | 任意文本 | 非空为真 |

### 类型提升

INT + FLOAT → FLOAT;  INT + INT → INT
STRING + STRING → 字符串拼接 (复用 `OP_ADD`)

### 局部变量

所有变量必须用 `local` 声明。无全局变量。

```
local x           → x = nil
local x = 10      → x = 10
local a, b = 1, 2 → a = 1, b = 2
```

变量作用域从声明点到所在块结束，内层可遮蔽外层。

### 控制流

```
if cond then
  ...
elseif cond2 then
  ...
else
  ...
end

while cond do
  ...
end

repeat
  ...
until cond

return expr    -- 从函数返回 (单返回值)
return         -- 返回 nil
```

---

## 5. 内置名 (编译期识别)

| 写法 | 行为 | 用途 |
|------|------|------|
| `print(x)` | 打印 x 并换行 | 调试输出 |
| `print(x, y)` | 打印 x, y (无空格) | 调试输出 |

> **注意**: `print` 不是真实函数, 是编译器识别 `print(...)` 后直接生成 `OP_PRINTLN`。`print` 被注册为 slot 0 的内置名, 不会触发函数调用。

---

## 6. AST 节点

### Expr

| 类型 | 字段 | 说明 |
|------|------|------|
| `NIL` | — | nil 字面量 |
| `BOOL` | `boolVal` | true / false |
| `NUMBER` | `numVal` | 数值字面量 |
| `STRING` | `strVal` | 字符串字面量 |
| `NAME` | `strVal` | 变量引用 |
| `UNARY` | `op` + `rhs` | 一元运算 |
| `BINARY` | `op` + `lhs` + `rhs` | 二元运算 |
| `CALL` | `callee` + `args` | 函数调用 |
| `FUNCDEF` | `params` + `body` | 匿名函数定义 |

### Stmt

| 类型 | 字段 | 说明 |
|------|------|------|
| `BLOCK` | `stmts` | 语句块 |
| `LOCAL_DECL` | `names` + `inits` | 局部变量声明 |
| `ASSIGN` | `name` + `value` | 变量赋值 |
| `IF` | `conds` + `bodies` + `elseBody` | 条件分支 |
| `WHILE` | `cond` + `body` | while 循环 |
| `REPEAT` | `body` + `cond` | repeat-until 循环 |
| `CALL` | `call` | 作为语句的函数调用 |
| `RETURN` | `value` | 返回语句 |

---

## 7. 状态总览

| 特性 | 解析 | 编译 | 运行 |
|------|------|------|------|
| `nil`, `true`, `false` | ✓ | ✓ | ✓ |
| Number 字面量 | ✓ | ✓ | ✓ |
| String 字面量 | ✓ | ✓ | ✓ |
| `+ - * / % ^` | ✓ | ✓ | ✓ |
| `== ~= < > <= >=` | ✓ | ✓ | ✓ |
| `and or not` | ✓ | ✓ | ✓ |
| `..` 字符串拼接 | ✓ | ✓ | ✓ |
| `(` `)` 分组 | ✓ | — | — |
| `-` `not` 一元 | ✓ | ✓ | ✓ |
| 变量名引用 | ✓ | ✓ | ✓ |
| 函数调用 `f(...)` | ✓ | ✓ | ✓ (仅 name) |
| 局部声明 `local x = v` | ✓ | ✓ | ✓ |
| 赋值 `x = v` | ✓ | ✓ | ✓ |
| `if / elseif / else` | ✓ | ✓ | ✓ |
| `while` | ✓ | ✓ | ✓ |
| `repeat / until` | ✓ | ✓ | ✓ |
| `return` | ✓ | ✓ | ✓ |
| 匿名函数 `function() end` | ✓ | ✓ | (占位) |
| 函数声明 `function name() end` | ✗ | — | — |
| 注释 `--` | ✓ | — | — |
| `;` 分隔 | ✓ | — | — |
| `#` 长度 | ✗ | — | — |
| `for` 循环 | ✗ | — | — |
| `break` | ✗ | — | — |
| Table `{ }` | ✗ | — | — |
| 索引 `t[k]` `t.k` | ✗ | — | — |
| 闭包 + upvalue | ✗ | ✗ | ✗ |
| 协程 | ✗ | ✗ | ✗ |

---

> 完整设计文档见 `LANG_DESIGN.md`, VM 架构见 `DESIGN.md`。
