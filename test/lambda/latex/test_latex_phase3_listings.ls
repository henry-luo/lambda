import latex: lambda.latex.latex

let ast = input("test/lambda/latex/fixtures/listings_profile.tex", {type: "latex"})^
let result = latex.render_result(ast, null)
let html = latex.render_to_html(ast, null)

"codes:"; [for (issue in result.diagnostics) issue.code]
"caption:"; contains(html, "Listing 1: Example")
"line numbers:"; contains(html, "latex-listing-number\">1</span>")
"source escaped:"; contains(html, "if (x &lt; 2)")
"label linked:"; contains(html, "href=\"#lst:example\">1</a>")
"inline escaped:"; contains(html, "a&lt;b&amp;c")
