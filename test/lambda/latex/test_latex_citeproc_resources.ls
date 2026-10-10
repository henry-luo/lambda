// Local styles, locales and CSL JSON share the package resource boundary (D7.1.2v2).
import latex: lambda.latex.latex
import html: lambda.latex.to_html
let base = "test/lambda/latex/fixtures/citeproc"
let source = "\\documentclass{article}\\usepackage{biblatex}\\addbibresource{references.json}\\begin{document}\\cite{json-book}\\printbibliography\\end{document}"
let ast = latex.parse_source(source, base ++ "/resources.tex")
let result = latex.render_result(ast, {base_uri: base, citeproc: {
    style: "local-style.csl", locales: {'en-US': "local-locale.xml"}}})
let text = html.to_html(result.elements);
[result.diagnostics == [], contains(text, "Local source"), contains(text, "href=\"#bib-json-book\""),
 contains(text, "font-style:italic"), contains(text, "id=\"bib-json-book\""),
 any([for (asset in result.assets) asset.kind == "csl-style" and ends_with(asset.source, "/local-style.csl")]),
 any([for (asset in result.assets) asset.kind == "csl-locale" and ends_with(asset.source, "/local-locale.xml")])];
let dependent = "<style xmlns=\"http://purl.org/net/xbiblio/csl\" version=\"1.0\"><info><link rel=\"independent-parent\" href=\"urn:lambda:parent\"/></info></style>"
let inherited = latex.render_result(ast, {base_uri: base, citeproc: {
    style_xml: dependent, parents: {'urn:lambda:parent': "local-style.csl"}}});
[inherited.diagnostics == [], inherited.metadata.bibliography.style_id == "urn:lambda:test:resources",
 any([for (asset in inherited.assets) asset.kind == "csl-parent"])];
let conflict_ast = latex.parse_source(replace(source, "{biblatex}", "[style=authoryear]{biblatex}"), "<conflict>")
let conflict = latex.render_result(conflict_ast, {base_uri: base, citeproc: {style: "ieee"}});
[any([for (issue in conflict.diagnostics) issue.code == "conflicting-citation-style"]),
 contains(html.to_html(conflict.elements), "Citation could not be processed")];
let missing = latex.render_result(ast, {base_uri: base, citeproc: {style: "missing-style.csl"}});
[any([for (issue in missing.diagnostics) issue.code == "missing-csl-resource"]),
 contains(html.to_html(missing.elements), "Citation could not be processed"),
 any([for (asset in missing.assets) asset.kind == "csl-style" and not asset.available])];
let missing_locale = latex.render_result(ast, {base_uri: base, citeproc: {
    style: "ieee", locales: {'en-US': "missing-locale.xml"}}});
[any([for (issue in missing_locale.diagnostics) issue.code == "missing-csl-resource"]),
 any([for (asset in missing_locale.assets) asset.kind == "csl-locale" and not asset.available])]
