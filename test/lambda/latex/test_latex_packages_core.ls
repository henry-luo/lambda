// Exercise package activation and the structured LaTeX result together.
import latex: lambda.latex.latex

let ast = input("test/lambda/latex/fixtures/common_packages.tex", {type: "latex"}) ^ { null }
let result = latex.render_result(ast, {base_uri: "test/lambda/latex/fixtures"})
let html = latex.render_to_html(ast, {base_uri: "test/lambda/latex/fixtures"})

"packages:"; [for (entry in result.packages) entry.key]
"diagnostics:"; [for (issue in result.diagnostics) issue.code]
"mixed color:"; index_of(html, "rgb(") != null
"scoped color:"; index_of(html, "style=\"color:#123456\">Inside") != null
"number:"; index_of(html, "12 345.67") != null
"quantity:"; index_of(html, "12 km") != null
"uncertainty:"; index_of(html, "1.23(4)") != null
"scientific:"; index_of(html, "1.25 × 10³") != null
"decimal column:"; index_of(html, "latex-si-column") != null
"shared width:"; index_of(html, "grid-template-columns:2ch 1ch 2ch") != null
"bibliography:"; index_of(html, "The TeXbook") != null
"alignment:"; index_of(html, "latex-align-row") != null
"list start:"; index_of(html, "start=\"3\"") != null
"no empty list item:"; index_of(html, "<li> </li>") == null
