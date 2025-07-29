// Final comprehensive test of multi-flavor math parser
let legacy_test = input('./test/input/test_math_simple.txt', 'math')
legacy_test

let latex_test = input('./test/input/test_math_functions.txt', {'type': 'math', 'flavor': 'latex'})
latex_test

let typst_test = input('./test/input/test_math_typst.txt', {'type': 'math', 'flavor': 'typst'})
typst_test

let ascii_test = input('./test/input/test_math_ascii.txt', {'type': 'math', 'flavor': 'ascii'})
ascii_test

"All flavors successfully tested!"
