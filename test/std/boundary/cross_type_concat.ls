// Test: Cross-Type Concatenation (++)
// Layer: 1 | Category: boundary | Covers: ++ across type pairs

// ===== String ++ String =====
type("hello" ++ " world")
0
type("" ++ "")
0
type("abc" ++ "def")
0

// ===== Array ++ Array =====
[1, 2] ++ [3, 4]
[] ++ [1]
[1] ++ []
[] ++ []

// ===== String ++ Other (auto-converts to string) =====
type("num:" ++ 42)
0
type("pi:" ++ 3.14)
0
type("val:" ++ true)
0
type("val:" ++ false)
0
type("val:" ++ null)
0

// ===== Other ++ String (auto-converts to string) =====
type(42 ++ "abc")
0
type(3.14 ++ "xyz")
0
type(true ++ "yes")
0
type(false ++ "no")
0

// ===== Invalid ++ combinations =====
42 ++ 10
3.14 ++ 2.71
true ++ false
null ++ null
42 ++ true
42 ++ null
42 ++ [1]
true ++ null
true ++ [1]
[1, 2] ++ 3
[1, 2] ++ true
[1, 2] ++ null

// ===== Verify string concatenation values =====
let r1 = "hello" ++ " " ++ "world"; (r1 == "hello world")
let r2 = "num:" ++ 42; (r2 == "num:42")
let r3 = "" ++ "abc"; (r3 == "abc")
let r4 = "val:" ++ true; (r4 == "val:true")
let r5 = "val:" ++ false; (r5 == "val:false")
let r6 = "val:" ++ null; (r6 == "val:")
let r7 = 42 ++ " items"; (r7 == "42 items")

// ===== Verify array concatenation values =====
let a1 = [1, 2] ++ [3, 4]; a1
let a2 = [] ++ []; a2
let a3 = [1] ++ [2] ++ [3]; a3
