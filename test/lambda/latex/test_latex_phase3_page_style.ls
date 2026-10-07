import latex: lambda.latex.latex
import util: lambda.latex.util

let ast = input("test/lambda/latex/fixtures/page_style_profile.tex", "latex") ^ { null }
let result = latex.render_result(ast, {target: "pdf", paged: true})
{unsupported: len([for (issue in result.diagnostics where issue.code == "unsupported-output") issue]),
 header: contains(util.text_of(result.body), "Functional documents"),
 margin_content: contains(result.stylesheet, "@top-left{white-space:pre-line;font-size:0.85em;content:"),
 page_number: contains(result.stylesheet, "content:counter(page)"),
 page_count: contains(result.stylesheet, "content:counter(pages)"),
 reference: contains(result.stylesheet, "target-counter(attr(href),page)"),
 print_band: contains(result.stylesheet, ".latex-running-footer{display:none}")}
