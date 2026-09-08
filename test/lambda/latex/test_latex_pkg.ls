// test_latex_pkg.ls — End-to-end functional test for the LaTeX package

import latex: lambda.latex.latex

// use a checked-in document so the functional test covers the parser and
// package renderer together.
let ast = input("test/input/test_input.tex", {type: "latex"}) ^ { null }

"=== LaTeX AST tags ==="
name(ast)
len(ast)

"=== Rendering ==="
latex.render_to_html(ast, {standalone: false, numbering: true})
