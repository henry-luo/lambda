// Advanced Math Parser Tests
// Tests enhanced Typst and ASCII functionality

"=== TYPST MATH TESTS ==="

// Test Typst frac() function
let typst_frac = input('./test/input/math_typst_frac.txt', {'type': 'math', 'flavor': 'typst'})
"Typst frac() function:"
typst_frac

// Test Typst function calls
let typst_funcs = input('./test/input/math_typst_funcs.txt', {'type': 'math', 'flavor': 'typst'})
"Typst function calls:"
typst_funcs

// Test Typst mixed expressions
let typst_mixed = input('./test/input/math_typst_mixed.txt', {'type': 'math', 'flavor': 'typst'})
"Typst mixed expressions:"
typst_mixed

"=== ASCII MATH TESTS ==="

// Test ASCII function calls
let ascii_funcs = input('./test/input/math_ascii_funcs.txt', {'type': 'math', 'flavor': 'ascii'})
"ASCII function calls:"
ascii_funcs

// Test ASCII power operations  
let ascii_power = input('./test/input/math_ascii_power.txt', {'type': 'math', 'flavor': 'ascii'})
"ASCII power operations:"
ascii_power

// Test ASCII mixed expressions
let ascii_mixed = input('./test/input/math_ascii_mixed.txt', {'type': 'math', 'flavor': 'ascii'})
"ASCII mixed expressions:"
ascii_mixed

"=== COMPARISON TESTS ==="

// Same expression in all three flavors
let latex_expr = input('./test/input/math_comparison.txt', {'type': 'math', 'flavor': 'latex'})
"LaTeX version:"
latex_expr

let typst_expr = input('./test/input/math_comparison.txt', {'type': 'math', 'flavor': 'typst'})
"Typst version:"
typst_expr

let ascii_expr = input('./test/input/math_comparison.txt', {'type': 'math', 'flavor': 'ascii'})
"ASCII version:"
ascii_expr

"✅ Advanced math parser features working!"
