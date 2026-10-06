// Source-order math rows retain tags, labels and numbering suppression.
import latex: lambda.latex.latex

let ast = input("test/lambda/latex/fixtures/amsmath_annotations.tex", {type: "latex"}) ^ { null }
let result = latex.render_result(ast, null)
let html = latex.render_to_html(ast, null)

"diagnostics:"; [for (issue in result.diagnostics) issue.code]
"numbered row:"; index_of(html, "latex-align-number") != null and index_of(html, "(1)") != null
"suppressed row:"; len(split(html, "latex-align-number")) == 3
"custom tag:"; index_of(html, "(Special)") != null
"bare tag:"; index_of(html, "Proof") != null and index_of(html, "(Proof)") == null
"operator:"; index_of(html, "Foo") != null
"first reference:"; index_of(html, "href=\"#eq:first\">1</a>") != null
"tag reference:"; index_of(html, "Equation Special") != null

let order_ast = input("test/lambda/latex/fixtures/amsmath_operator_order.tex", {type: "latex"}) ^ { null }
let order_result = latex.render_result(order_ast, null)
let order_html = latex.render_to_html(order_ast, null);
"operator issues:"; [for (issue in order_result.diagnostics) issue.code]
"operator issue locations:"; all([for (issue in order_result.diagnostics) issue.offset != null])
"operator source order:"; len(split(order_html, "Foo")) == 2
