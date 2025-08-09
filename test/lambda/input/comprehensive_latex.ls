// Comprehensive LaTeX test for robustness testing
let latex = input('./test/input/comprehensive.tex', 'latex')

"LaTeX parsing result:"
latex

"Formatting LaTeX as JSON (first few elements):"
format(latex, 'json')

"Formatting LaTeX as LaTeX:"
format(latex, 'latex')

"Comprehensive test completed."
