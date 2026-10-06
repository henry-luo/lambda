import latex: lambda.latex.latex

let ast = input("test/lambda/latex/fixtures/cleveref_profile.tex", {type: "latex"})^
let result = latex.render_result(ast, null)
let html = latex.render_to_html(ast, null)

"codes:"; [for (issue in result.diagnostics) issue.code]
"range:"; contains(html, "sections\u00a0<a class=\"latex-ref latex-cref\" href=\"#sec:first\">1</a>–<a class=\"latex-ref latex-cref\" href=\"#sec:third\">3</a>")
"capitalized:"; contains(html, "Sections\u00a0<a class=\"latex-ref latex-cref\" href=\"#sec:first\">1</a> and <a class=\"latex-ref latex-cref\" href=\"#sec:third\">3</a>")
"unresolved visible:"; contains(html, "latex-ref latex-unresolved\">??</span>")
