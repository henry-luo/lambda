// Comprehensive ASCII Math Parser Test Suite
// Tests all major functionality of the standalone ASCII math parser

"=== COMPREHENSIVE ASCII MATH PARSER TESTS ==="

// Test 1: Basic arithmetic
let basic_test = input('test/input/ascii_math_basic.txt', {'type': 'math', 'flavor': 'ascii'})
"1. Basic arithmetic (x + y):"
basic_test

// Test 2: Functions
let function_test = input('test/input/ascii_math_functions.txt', {'type': 'math', 'flavor': 'ascii'})
"2. Trigonometric functions (sin(x) + cos(y)):"
function_test

// Test 3: Powers and subscripts
let power_test = input('test/input/ascii_math_powers.txt', {'type': 'math', 'flavor': 'ascii'})
"3. Powers and subscripts (x^2 + y_1^3):"
power_test

// Test 4: Greek symbols
let symbol_test = input('test/input/ascii_math_symbols.txt', {'type': 'math', 'flavor': 'ascii'})
"4. Greek symbols (alpha + beta = gamma):"
symbol_test

// Test 5: Fractions
let fraction_test = input('test/input/ascii_math_fractions.txt', {'type': 'math', 'flavor': 'ascii'})
"5. Fractions (x/y + (a+b)/(c+d)):"
fraction_test

"=== ALL TESTS COMPLETE ==="
