// Test standalone ASCII math formatter
"=== STANDALONE ASCII MATH FORMATTER TESTS ==="

// Test 1: Basic addition
let basic_math = input('test/input/ascii_math_standalone_test.txt', {'type': 'math', 'flavor': 'ascii'})
"Basic math AST:"
basic_math

// Test 2: Simple expression with functions
let func_expr = input('sin(x) + cos(y)', {'type': 'math', 'flavor': 'ascii'})
"Function expression AST:"
func_expr

// Test 3: Greek letters
let greek_expr = input('alpha + beta = gamma', {'type': 'math', 'flavor': 'ascii'})
"Greek letters AST:"
greek_expr
