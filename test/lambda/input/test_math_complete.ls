// Comprehensive Math Parser Test
// Tests advanced features, multiple matrix types, and error handling

// Test all matrix environment types
let matrix_environments = input('./test/input/matrix_all_types.txt', {'type': 'math', 'flavor': 'latex'})
"All matrix environments:"
matrix_environments

// Test complex mathematical expressions
let complex_math = input('./test/input/math_complex.txt', {'type': 'math', 'flavor': 'latex'})
"Complex mathematics:"
complex_math

// Test mixed matrix and expression syntax
let mixed_syntax = input('./test/input/math_mixed_syntax.txt', {'type': 'math', 'flavor': 'latex'})
"Mixed syntax:"
mixed_syntax

// Test Greek letters and advanced symbols
let greek_symbols = input('./test/input/math_greek_symbols.txt', {'type': 'math', 'flavor': 'latex'})
"Greek letters:"
greek_symbols

// Test advanced functions and environments
let advanced_functions = input('./test/input/math_advanced_functions.txt', {'type': 'math', 'flavor': 'latex'})
"Advanced functions:"
advanced_functions

// Test flavor support (basic fallback for non-LaTeX flavors)
let basic_flavor = input('./test/input/math_simple.txt', {'type': 'math', 'flavor': 'basic'})
"Basic flavor:"
basic_flavor

// Test other flavor types with simple expressions
let typst_flavor = input('./test/input/math_simple.txt', {'type': 'math', 'flavor': 'typst'})
"Typst flavor:"
typst_flavor

let ascii_flavor = input('./test/input/math_simple.txt', {'type': 'math', 'flavor': 'ascii'})
"ASCII flavor:"
ascii_flavor

// Test error handling with malformed input
let error_test = input('./test/input/math_malformed_matrix.txt', {'type': 'math', 'flavor': 'latex'})
"Error handling (malformed matrix):"
error_test

"✅ Comprehensive math parser test completed!"
