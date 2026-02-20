// Test: Cross-Type Arithmetic
// Layer: 1 | Category: boundary | Covers: +, -, *, /, %, ^, div across type pairs

// ===== Addition (+) =====
// Numeric promotion: int + float → float
42 + 3.14
0 + 0.0
-5 + 2.5
// Invalid cross-type addition
42 + "hello"
"hello" + 42
42 + true
true + 42
42 + null
null + 42
3.14 + "hello"
"hello" + 3.14
3.14 + true
true + 3.14
3.14 + null
null + 3.14
"hello" + true
true + "hello"
"hello" + null
null + "hello"
true + null
null + true
true + false
false + true
null + null

// ===== Subtraction (-) =====
// Numeric promotion: int - float → float
42 - 3.14
0 - 0.0
// Invalid cross-type subtraction
42 - "hello"
42 - true
42 - null
3.14 - "hello"
3.14 - true
3.14 - null
"hello" - "world"
true - false
null - null

// ===== Multiplication (*) =====
// Numeric promotion: int * float → float
42 * 3.14
0 * 0.0
-3 * 2.5
// Invalid cross-type multiplication
42 * "hello"
42 * true
42 * null
3.14 * "hello"
3.14 * true
3.14 * null
"hello" * "world"
true * true
null * null

// ===== Division (/) =====
// Numeric promotion: int / float → float
42 / 3.14
0 / 1.0
// Invalid cross-type division
42 / "hello"
42 / true
42 / null
3.14 / "hello"
3.14 / true
3.14 / null
"hello" / "world"
true / false
null / null

// ===== Modulo (%) =====
// Only int × int works
42 % 5
-7 % 3
// Invalid modulo combinations
42 % 5.0
3.14 % 2
3.14 % 2.0
42 % "hello"
42 % true
42 % null
"hello" % 2
true % 2
null % 2

// ===== Exponentiation (^) =====
// Works across numeric types
2 ^ 10
2 ^ 3.5
2.0 ^ 3
2.0 ^ 3.0
0 ^ 0
1 ^ 100
// Invalid exponent combinations
2 ^ "hello"
2 ^ true
2 ^ null
"hello" ^ 2
true ^ 2
null ^ 2

// ===== Integer Division (div) =====
// Only int × int works
42 div 5
-7 div 2
100 div 3
// Invalid div combinations
42 div 5.0
42.0 div 5
42 div "hello"
42 div true
42 div null
"hello" div 2
true div 2
null div 2
