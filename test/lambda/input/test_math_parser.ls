// Test multi-flavor math parsing
let test_latex = input('./test/input/test_math_functions.txt', {'type': 'math', 'flavor': 'latex'})
"LaTeX math functions:"
test_latex

let test_typst = input('./test/input/test_math_typst.txt', {'type': 'math', 'flavor': 'typst'})
"Typst math:"
test_typst

let test_ascii = input('./test/input/test_math_ascii.txt', {'type': 'math', 'flavor': 'ascii'})
"ASCII math:"
test_ascii

// Test legacy API (should default to LaTeX)
let legacy_test = input('./test/input/test_math_simple.txt', 'math')
"Legacy API test:"
legacy_test

"Multi-flavor math parser test completed!"
