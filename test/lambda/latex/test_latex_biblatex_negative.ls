// Unsupported and malformed bibliography requests remain visible and located.
import latex: lambda.latex.latex
import html_ser: lambda.latex.to_html

let base = "test/lambda/latex/fixtures"
let ast = input("test/lambda/latex/fixtures/biblatex_negative.tex", {type: "latex"}) ^ { null }
let result = latex.render_result(ast, {base_uri: base})
let html = html_ser.to_html(result.elements)
let codes = [for (issue in result.diagnostics) issue.code]
let expected = ["missing-bib-resource", "duplicate-bib-key", "unsupported-bib-entry-type",
    "undefined-bib-string", "cyclic-bib-inheritance", "missing-bib-parent",
    "malformed-bib-record", "unresolved-citation", "unsupported-citation-star",
    "data-only-bib-citation",
    "unsupported-bib-heading", "unsupported-bib-filter-type",
    "natbib-alias-disabled", "unsupported-bib-language"]

"expected diagnostics:"; [for (code in expected) any([for (issue in codes) issue == code])]
"file and line:"; all([for (issue in result.diagnostics
    where issue.file != null) issue.line != null and issue.line > 0])
"unsupported output:"; index_of(html, "data-latex-error") != null
"unknown type absent:"; index_of(html, "Unknown Type") == null
"invalid print target absent:"; index_of(html, "id=\"bib-duplicate\"") == null and
    index_of(html, "href=\"#bib-duplicate\"") == null

let bad_ast = input("test/lambda/latex/fixtures/biblatex_bad_style.tex", {type: "latex"}) ^ { null }
let bad = latex.render_result(bad_ast, {base_uri: base})
let bad_html = html_ser.to_html(bad.elements)
"invalid style codes:"; [for (issue in bad.diagnostics) issue.code]
"invalid style output:"; index_of(bad_html, "Unsupported biblatex profile") != null

let locale_ast = input("test/lambda/latex/fixtures/biblatex_locale.tex", {type: "latex"}) ^ { null }
let unsupported_locale = latex.render_result(locale_ast, {base_uri: base, language: "spanish"})
let locale_html = html_ser.to_html(unsupported_locale.elements)
"unsupported document language:"; any([for (issue in unsupported_locale.diagnostics)
    issue.code == "unsupported-bib-language"]) and
    index_of(locale_html, "Unsupported biblatex profile") != null

let invalid_opts_source = replace(input("test/lambda/latex/fixtures/biblatex_bad_style.tex", "text") ^ { "" },
    "style=apa,sorting=locale,backend=bibtex",
    "style=authoryear,sorting=nyt,backend=biber,uniquename=guess,maxnames=0")
let invalid_opts_ast = parse(invalid_opts_source, {type: "latex"}) ^ { null }
let invalid_opts = latex.render_result(invalid_opts_ast, {base_uri: base})
let invalid_opts_html = html_ser.to_html(invalid_opts.elements)
"invalid options blocked:"; any([for (issue in invalid_opts.diagnostics)
    issue.code == "unsupported-bib-uniquename"]) and
    any([for (issue in invalid_opts.diagnostics) issue.code == "invalid-bib-name-limit"]) and
    index_of(invalid_opts_html, "Unsupported biblatex profile") != null
