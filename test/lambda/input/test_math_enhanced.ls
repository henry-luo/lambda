// Enhanced Math Parser Features Test
// Tests newly added mathematical expressions: absolute value, ceiling/floor, prime notation, number sets, set/logic operators

"=== ENHANCED MATH FEATURES TEST ==="

// === ABSOLUTE VALUE TESTS ===
"=== ABSOLUTE VALUE TESTS ==="

// Test LaTeX absolute value with \abs{} command
let abs_latex_cmd = input('./test/input/math_abs_simple.txt', {'type': 'math', 'flavor': 'latex'})
"LaTeX \\abs{} command:"
abs_latex_cmd

// Test LaTeX absolute value with \left| \right| delimiters
let abs_latex_delim = input('./test/input/math_abs_latex.txt', {'type': 'math', 'flavor': 'latex'})
"LaTeX \\left| \\right| delimiters:"
abs_latex_delim

// Test ASCII absolute value
let abs_ascii = input('./test/input/math_abs_ascii.txt', {'type': 'math', 'flavor': 'ascii'})
"ASCII absolute value:"
abs_ascii

// === CEILING AND FLOOR TESTS ===
"=== CEILING AND FLOOR TESTS ==="

// Test LaTeX ceiling and floor functions
let ceil_floor_latex = input('./test/input/math_ceil_floor_latex.txt', {'type': 'math', 'flavor': 'latex'})
"LaTeX ceiling/floor (\\lceil, \\lfloor):"
ceil_floor_latex

// Test ASCII ceiling and floor functions
let ceil_floor_ascii = input('./test/input/math_ceil_floor_ascii.txt', {'type': 'math', 'flavor': 'ascii'})
"ASCII ceiling/floor (ceil, floor):"
ceil_floor_ascii

// === PRIME NOTATION TESTS ===
"=== PRIME NOTATION TESTS ==="

// Test prime notation (derivatives)
let prime_notation = input('./test/input/math_prime.txt', {'type': 'math', 'flavor': 'ascii'})
"Prime notation (f', f'', f'''):"
prime_notation

// === NUMBER SETS TESTS ===
"=== NUMBER SETS TESTS ==="

// Test mathematical number sets
let number_sets = input('./test/input/math_sets.txt', {'type': 'math', 'flavor': 'latex'})
"Number sets (\\mathbb{R}, \\mathbb{C}, etc.):"
number_sets

// === SET AND LOGIC OPERATORS TESTS ===
"=== SET AND LOGIC OPERATORS TESTS ==="

// Test set theory and logic operators
let logic_operators = input('./test/input/math_logic.txt', {'type': 'math', 'flavor': 'latex'})
"Logic operators (\\forall, \\exists, \\in, etc.):"
logic_operators

// === COMPREHENSIVE ENHANCED EXPRESSIONS ===
"=== COMPREHENSIVE TESTS ==="

// Test mixed enhanced expressions
let enhanced_mixed = input('./test/input/math_enhanced_mixed.txt', {'type': 'math', 'flavor': 'latex'})
"Mixed enhanced expressions:"
enhanced_mixed

// Test power operations with new features
let power_enhanced = input('./test/input/math_power_enhanced.txt', {'type': 'math', 'flavor': 'ascii'})
"Power operations with enhanced features:"
power_enhanced

// === COMPARISON ACROSS FLAVORS ===
"=== FLAVOR COMPARISON ==="

// Test absolute value across all flavors
let abs_comparison_latex = input('./test/input/math_abs_comparison.txt', {'type': 'math', 'flavor': 'latex'})
"Absolute value - LaTeX:"
abs_comparison_latex

let abs_comparison_ascii = input('./test/input/math_abs_comparison.txt', {'type': 'math', 'flavor': 'ascii'})
"Absolute value - ASCII:"
abs_comparison_ascii

let abs_comparison_typst = input('./test/input/math_abs_comparison.txt', {'type': 'math', 'flavor': 'typst'})
"Absolute value - Typst:"
abs_comparison_typst

"✅ Enhanced math features test complete!"
"✅ New features: absolute value, ceiling/floor, prime notation, number sets, logic operators!"
