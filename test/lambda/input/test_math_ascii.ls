// ASCII Math Parser Test Suite
// Tests the standalone ASCII math parser implementation

"=== ASCII MATH PARSER TESTS ==="

// Basic expressions
let basic_test = input('x + y', {'type': 'math', 'flavor': 'ascii'})
"Basic addition:"
basic_test

// Functions
let function_test = input('sin(x) + cos(y)', {'type': 'math', 'flavor': 'ascii'})
"Trigonometric functions:"
function_test

// Fractions
let fraction_test = input('x/y + (a+b)/(c+d)', {'type': 'math', 'flavor': 'ascii'})
"Fractions:"
fraction_test

// Powers and subscripts
let power_test = input('x^2 + y_1^3', {'type': 'math', 'flavor': 'ascii'})
"Powers and subscripts:"
power_test

// Greek letters and symbols  
let symbol_test = input('alpha + beta = gamma', {'type': 'math', 'flavor': 'ascii'})
"Greek symbols:"
symbol_test

// Roots
let root_test = input('sqrt(x) + sqrt(y)', {'type': 'math', 'flavor': 'ascii'})
"Square roots:"
root_test

// Numbers
let number_test = input('2.5 + 3.14159', {'type': 'math', 'flavor': 'ascii'})
"Numbers:"
number_test

// Relations
let relation_test = input('x = y', {'type': 'math', 'flavor': 'ascii'})
"Relations:"
relation_test

// Complex expression
let complex_test = input('sin(alpha) + x^2 = sqrt(pi)', {'type': 'math', 'flavor': 'ascii'})
"Complex expression:"
complex_test

"=== ASCII MATH PARSER TESTS COMPLETE ==="
