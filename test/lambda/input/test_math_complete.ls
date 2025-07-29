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

// === NEW ENHANCED LATEX MATH ENVIRONMENTS ===

// Test cases environment for piecewise functions
let cases_test = input('./test/input/math_cases.txt', {'type': 'math', 'flavor': 'latex'})
"Cases environment (piecewise functions):"
cases_test

// Test equation environment for numbered equations
let equation_test = input('./test/input/math_equation.txt', {'type': 'math', 'flavor': 'latex'})
"Equation environment (numbered):"
equation_test

// Test align environment for aligned equations
let align_test = input('./test/input/math_align.txt', {'type': 'math', 'flavor': 'latex'})
"Align environment (aligned equations):"
align_test

// Test aligned environment (used within other environments)
let aligned_test = input('./test/input/math_aligned.txt', {'type': 'math', 'flavor': 'latex'})
"Aligned environment (inline aligned):"
aligned_test

// Test gather environment for centered equations
let gather_test = input('./test/input/math_gather.txt', {'type': 'math', 'flavor': 'latex'})
"Gather environment (centered equations):"
gather_test

// Test smallmatrix environment for inline matrices
let smallmatrix_test = input('./test/input/math_smallmatrix.txt', {'type': 'math', 'flavor': 'latex'})
"Smallmatrix environment (inline matrix):"
smallmatrix_test

// Test comprehensive advanced environments together
let advanced_environments = input('./test/input/math_advanced_environments.txt', {'type': 'math', 'flavor': 'latex'})
"Advanced environments combined:"
advanced_environments

// === END NEW ENHANCED FEATURES ===

// === NEWLY ENHANCED MATH FEATURES ===
"=== NEWLY ENHANCED MATH FEATURES ==="

// Test enhanced absolute value functions
let enhanced_abs = input('./test/input/math_abs_simple.txt', {'type': 'math', 'flavor': 'latex'})
"Enhanced absolute value:"
enhanced_abs

// Test enhanced ceiling/floor functions  
let enhanced_ceil_floor = input('./test/input/math_ceil_floor_latex.txt', {'type': 'math', 'flavor': 'latex'})
"Enhanced ceiling/floor:"
enhanced_ceil_floor

// Test prime notation for derivatives
let enhanced_prime = input('./test/input/math_prime.txt', {'type': 'math', 'flavor': 'ascii'})
"Enhanced prime notation:"
enhanced_prime

// Test mathematical number sets
let enhanced_sets = input('./test/input/math_sets.txt', {'type': 'math', 'flavor': 'latex'})
"Enhanced number sets:"
enhanced_sets

// Test set theory and logic operators
let enhanced_logic = input('./test/input/math_logic.txt', {'type': 'math', 'flavor': 'latex'})  
"Enhanced logic operators:"
enhanced_logic

// === END NEWLY ENHANCED FEATURES ===

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
"✅ Enhanced LaTeX environments (cases, equation, align, aligned, gather, smallmatrix) tested!"
"✅ Newly enhanced features (abs, ceil/floor, prime, sets, logic) tested!"
