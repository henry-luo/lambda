import latex: lambda.latex.latex

let ast = input("test/lambda/latex/fixtures/macro_nested_profile.tex", {type: "latex"})^
let result = latex.render_result(ast, null)
let html = latex.render_to_html(ast, null)

"codes:"; [for (issue in result.diagnostics) issue.code]
"inline style:"; contains(html, "<strong>strong</strong>")
"frame:"; contains(html, "class=\"latex-tcolorbox\"")
"nested style:"; contains(html, "<strong>Framed words</strong>")
"option text absent:"; not contains(html, "[colback=")
