// Minimal test to isolate null byte error
let latex1 = input('./test/isolated/minimal_crash_test.tex', 'latex')
format(latex1, 'json')
