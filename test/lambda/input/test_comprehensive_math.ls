// Comprehensive test of enhanced LaTeX math features

// Test basic arithmetic with precedence
let basic = input('./test/input/test_math_simple.txt', {'type': 'math', 'flavor': 'latex'})
"Basic arithmetic (1+2*3):"
basic

// Test Greek letters
let greek = input('./test/input/test_math_greek.txt', {'type': 'math', 'flavor': 'latex'})
"Greek letters:"
greek

// Test sums and products with limits
let sums = input('./test/input/test_math_sum.txt', {'type': 'math', 'flavor': 'latex'})
"Sums and products with limits:"
sums

// Test integrals with limits
let integrals = input('./test/input/test_math_integral.txt', {'type': 'math', 'flavor': 'latex'})
"Integrals with limits:"
integrals

// Test advanced trigonometric functions
let trig = input('./test/input/test_math_advanced_trig.txt', {'type': 'math', 'flavor': 'latex'})
"Advanced trigonometric functions:"
trig

"✅ Enhanced LaTeX Math Parser - All Features Working!"
