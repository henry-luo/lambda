let test_typst = input('./test/input/test_math_simple.txt', {'type': 'math', 'flavor': 'typst'})
test_typst

let test_ascii = input('./test/input/test_math_simple.txt', {'type': 'math', 'flavor': 'ascii'})  
test_ascii

let test_latex_explicit = input('./test/input/test_math_functions.txt', {'type': 'math', 'flavor': 'latex'})
test_latex_explicit

"Enhanced input function tests completed!"
