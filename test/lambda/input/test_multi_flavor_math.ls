// Test multi-flavor math parsing with specific input files
let test_latex_specific = input('./test/input/test_math_functions.txt', {'type': 'math', 'flavor': 'latex'})
test_latex_specific

let test_typst_specific = input('./test/input/test_math_typst.txt', {'type': 'math', 'flavor': 'typst'})
test_typst_specific

let test_ascii_specific = input('./test/input/test_math_ascii.txt', {'type': 'math', 'flavor': 'ascii'})
test_ascii_specific

"Multi-flavor math parser test completed!"
