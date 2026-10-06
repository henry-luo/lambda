// Phase 2 entry families, name forms, field inheritance and plural citations.
import latex: lambda.latex.latex
import html_ser: lambda.latex.to_html
import bib_data: lambda.latex.packages.bib_data
import bib_names: lambda.latex.packages.bib_names

let base = "test/lambda/latex/fixtures"
let ast = input("test/lambda/latex/fixtures/biblatex_types.tex", {type: "latex"}) ^ { null }
let loaded = bib_data.load(ast, base)
let result = latex.render_result(ast, {base_uri: base})
let html = html_ser.to_html(result.elements)
let types = ["book", "article", "inbook", "incollection", "inproceedings",
    "thesis", "report", "online", "misc", "unpublished"]

"entry types:"; [for (kind in types)
    any([for (entry in loaded.entries) entry.kind == kind])]
"data diagnostics:"; [for (issue in loaded.diagnostics) issue.code]
"render diagnostics:"; [for (issue in result.diagnostics) issue.code]
"all entries printed:"; all([for (key in ["parent", "article", "inbook", "collection",
    "proceedings", "thesis", "report", "online", "misc", "unpublished"])
    index_of(html, "id=\"bib-" ++ key ++ "\"") != null])
"type details:"; [for (value in ["In: Math Review 4(2), 12--19",
    "Chapter One", "Collected Notes", "Archive Press", "Paris University",
    "Logic Meeting", "Annual Report", "ISBN: 978-1-2345-6789-0",
    "arXiv:1234"])
    index_of(html, value) != null]
"external links:"; index_of(html, "href=\"https://doi.org/10.1000/article\"") != null and
    index_of(html, "href=\"https://example.org/archive\"") != null
"fullcite identifiers:"; len(split(html, "https://doi.org/10.1000/article")) >= 3
"basic citation commands:"; [for (value in [
    ">“On Computing”</a>", ">de La Fontaine</a>", ">2020</a>",
    ">de La Fontaine, Jean (2020). “On Computing”"])
    index_of(html, value) != null]
"plural citations:"; [for (value in [
    ">Lovelace (1843a)</a>", ">de La Fontaine (2020)</a>",
    "(cf. ", "p. 4)", "p. 5)",
    ">Curie 1903</a>)", ">The Example Consortium 2021</a>)"])
    index_of(html, value) != null]

let protected_ast = input("test/lambda/latex/fixtures/biblatex_phase2.tex", {type: "latex"}) ^ { null }
let protected = bib_data.load(protected_ast, base)
let ada = [for (entry in protected.entries where entry.key == "ada1843") entry][0]
"protected title:"; ada.raw_fields.title == "Notes on the {Analytical Engine}" and
    ada.fields.title == "Notes on the Analytical Engine"
"string concat:"; any([for (entry in protected.entries where entry.key == "smith2020a")
    entry.fields.journaltitle == "Math Notes"])
"cross-file resources:"; len(protected.entries) == 10

let names = bib_names.parse("Jean de La Fontaine and Smith, Jr., John and {The Example Consortium}")
"name forms:"; [names[0].given, names[0].prefix, names[0].family,
    names[1].family, names[1].suffix, names[1].given, names[2].literal]
