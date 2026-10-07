import latex: lambda.latex.latex
import html: lambda.latex.to_html

let ast = input("test/lambda/latex/fixtures/hyperref_bookmarks.tex", "latex") ^ { null }
let result = latex.render_result(ast, {standalone: true, target: "pdf"})
let serialized = html.to_html(result.elements) ^ { "" }
{unsupported: len([for (issue in result.diagnostics where issue.code == "unsupported-output") issue]),
 explicit: contains(serialized, "data-pdf-outline-title=\"Préface\""),
 numbered: contains(serialized, "data-pdf-outline-title=\"1 Introduction\""),
 hierarchy: contains(serialized, "data-pdf-outline-level=\"2\""),
 destination: contains(serialized, "id=\"pdfbookmark-preface\"")}
