// test_math_html_output.ls — Test LaTeX-to-HTML rendering pipeline
// Loads math_intensive_test.tex and outputs the transformed HTML string.

import latex: lambda.package.latex.latex

let html = latex.render_file_to_html("test/input/math_intensive_test.tex")
html
