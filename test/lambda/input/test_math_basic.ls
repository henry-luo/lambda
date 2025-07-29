// Basic Math Parser Test
// Tests essential functionality with LaTeX flavor

// Test basic matrix environments
let matrix_simple = input('./test/input/matrix_basic.txt', {'type': 'math', 'flavor': 'latex'})
"Basic matrix:"
matrix_simple

let matrix_paren = input('./test/input/matrix_paren.txt', {'type': 'math', 'flavor': 'latex'})
"Parentheses matrix:"
matrix_paren

// Test legacy API (should default to LaTeX)
let legacy_test = input('./test/input/math_simple.txt', 'math')
"Legacy API test:"
legacy_test

// Test basic LaTeX commands
let basic_latex = input('./test/input/math_latex_basic.txt', {'type': 'math', 'flavor': 'latex'})
"Basic LaTeX:"
basic_latex

"✅ Basic math parser functionality working!"
