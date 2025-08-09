// Test for LaTeX parsing and formatting
// This script tests LaTeX input parsing and various output formats

let latex = input('./test/input/test.tex', 'latex')

"LaTeX parsing result:"
latex

"Formatting LaTeX as JSON:"
format(latex, 'json')

"Formatting LaTeX as LaTeX:"
format(latex, 'latex')

"Test completed."