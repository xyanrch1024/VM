-- Closure-based calculator demo for MiniLua
-- Uses first-class functions with upvalues

local function makeBinOp(op)
  return function(a, b)
    if op == "+" then return a + b end
    if op == "-" then return a - b end
    if op == "*" then return a * b end
    if op == "/" then return a / b end
    if op == "^" then return a ^ b end
    return nil
  end
end

local add = makeBinOp("+")
local sub = makeBinOp("-")
local mul = makeBinOp("*")
local div = makeBinOp("/")
local pow = makeBinOp("^")

local function fact(n)
  if n <= 1 then return 1 end
  return n * fact(n - 1)
end

local function fib(n)
  if n <= 1 then return n end
  return fib(n - 1) + fib(n - 2)
end

print(add(10, 20))
print(sub(100, 37))
print(mul(6, 7))
print(div(100, 4))
print(pow(2, 10))
print(fact(5))
print(fib(10))
