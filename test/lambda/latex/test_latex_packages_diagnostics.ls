import latex: lambda.latex.latex

let ast = input("test/lambda/latex/fixtures/package_diagnostics.tex", {type: "latex"}) ^ { null }
let result = latex.render_result(ast, {base_uri: "test/lambda/latex/fixtures"})
let html = latex.render_to_html(ast, {base_uri: "test/lambda/latex/fixtures"})
let codes = [for (issue in result.diagnostics) issue.code]

"codes:"; codes
"all located:"; all([for (issue in result.diagnostics) issue.offset != null])
"resume skips nested:"; index_of(html, "start=\"3\"") != null
"item spacing:"; index_of(html, "--latex-itemsep:1.99253pt") != null
"page color:"; index_of(html, "background-color:#ffffff") != null
"no invalid css:"; index_of(html, "color:error") == null

let target_ast = input("test/lambda/latex/fixtures/target_profiles.tex", {type: "latex"}) ^ { null }
let target_result = latex.render_result(target_ast, {target: "pdf"});
"pdf profile codes:"; [for (issue in target_result.diagnostics) issue.code]
"pdf diagnostics located:"; all([for (issue in target_result.diagnostics) issue.offset != null])

let bib_ast = input("test/lambda/latex/fixtures/biblatex_options.tex", {type: "latex"}) ^ { null }
let bib_result = latex.render_result(bib_ast, null);
"bib options:"; [for (issue in bib_result.diagnostics) issue.code]
"bib options located:"; all([for (issue in bib_result.diagnostics) issue.offset != null])
