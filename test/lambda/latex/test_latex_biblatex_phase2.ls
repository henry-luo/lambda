// Phase 2 bibliography profiles, selections, scopes and diagnostics.
import latex: lambda.latex.latex
import html_ser: lambda.latex.to_html
import bib_style: lambda.latex.packages.bib_style

let source = input("test/lambda/latex/fixtures/biblatex_phase2.tex", "text") ^ { "" }
let base = "test/lambda/latex/fixtures"
let styles = ["numeric", "numeric-comp", "authoryear", "authoryear-comp",
    "alphabetic", "authortitle"]

for (profile in styles) {
    let ast = parse(replace(source, "style=numeric", "style=" ++ profile), {type: "latex"}) ^ { null }
    let result = latex.render_result(ast, {base_uri: base})
    let html = html_ser.to_html(result.elements)
    profile; [for (issue in result.diagnostics) issue.code ++ ":" ++ string(issue.item)]
    "has cited text:"; index_of(html, "Analytical Engine") != null
    "has scope target:"; index_of(html, "bib-s1-p3-ada1843") != null
    "has segment target:"; index_of(html, "href=\"#bib-s0-p4-ada1844\"") != null
    "has filtered list:"; index_of(html, "Main sources") != null
    "type and notkeyword:"; index_of(html, "bib-s0-p6-web2022") != null
    "bibintoc target:"; index_of(html, "href=\"#bibliography-7\"") != null
    "has DOI link:"; index_of(html, "https://doi.org/10.1000/example") != null
    "has inherited publisher:"; index_of(html, "Example Press") != null
    "has footcite:"; index_of(html, "href=\"#fn-2\"") != null
    "uncited hidden:"; index_of(html, "Uncited Marker") == null
    "numeric range:"; if (profile == "numeric-comp") index_of(html, "–") != null else true
    "author year comp:"; if (profile == "authoryear-comp")
        index_of(html, "Lovelace 1843") != null and index_of(html, ">1844</a>") != null else true
    "citation name limit:"; if (profile == "authoryear")
        index_of(html, "Lovelace et al. 2025") != null else true
}

let all_ast = input("test/lambda/latex/fixtures/biblatex_nocite.tex", {type: "latex"}) ^ { null }
let all_result = latex.render_result(all_ast, {base_uri: base})
let all_html = html_ser.to_html(all_result.elements)
"nocite all:"; index_of(all_html, "Uncited Marker") != null
"xdata hidden:"; index_of(all_html, "bib-common") == null

let keyed_source = replace(input("test/lambda/latex/fixtures/biblatex_nocite.tex", "text") ^ { "" },
    "\\nocite{*}", "\\nocite{unused2024}")
let keyed_ast = parse(keyed_source, {type: "latex"}) ^ { null }
let keyed_html = html_ser.to_html(latex.render_result(keyed_ast, {base_uri: base}).elements)
"nocite keyed:"; index_of(keyed_html, "Uncited Marker") != null and
    index_of(keyed_html, "Notes on the Analytical Engine") == null

let locale_ast = input("test/lambda/latex/fixtures/biblatex_locale.tex", {type: "latex"}) ^ { null }
let german = latex.render_result(locale_ast, {base_uri: base})
let german_html = html_ser.to_html(german.elements)
let french = latex.render_result(locale_ast, {base_uri: base, language: "french"})
let french_html = html_ser.to_html(french.elements)
"German heading:"; index_of(german_html, "<h2>Literatur</h2>") != null
"French heading:"; index_of(french_html, "<h2>Références</h2>") != null
"entry language:"; index_of(german_html, "consulté le") != null
"German et al:"; index_of(german_html, "u. a.") != null
"given initials:"; index_of(german_html, "Lovelace, A.") != null
"link toggles:"; index_of(german_html, "https://doi.org/") == null and
    index_of(german_html, "https://example.org/archive") == null and
    index_of(german_html, "ISBN:") == null

let order_entries = [
    {key: "a", fields: {author: "Ada Alpha", title: "Zeta", year: "2021"}},
    {key: "b", fields: {author: "Ada Alpha", title: "Beta", year: "2022"}},
    {key: "c", fields: {author: "Bea Beta", title: "Alpha", year: "2020"}}
]
"sorting none:"; [for (entry in bib_style.sort_entries(order_entries, "none")) entry.key]
"sorting nty:"; [for (entry in bib_style.sort_entries(order_entries, "nty")) entry.key]
"sorting nyt:"; [for (entry in bib_style.sort_entries(order_entries, "nyt")) entry.key]
"sorting ynt:"; [for (entry in bib_style.sort_entries(order_entries, "ynt")) entry.key]

let mixed_source = replace(source, "style=numeric", "style=authoryear,citestyle=numeric,bibstyle=authoryear")
let mixed_ast = parse(mixed_source, {type: "latex"}) ^ { null }
let mixed = latex.render_result(mixed_ast, {base_uri: base})
let mixed_html = html_ser.to_html(mixed.elements)
"independent styles:"; index_of(mixed_html, ">1</a>") != null and
    index_of(mixed_html, "Lovelace, Ada (1843)") != null

let no_unique_source = replace(replace(source, "style=numeric", "style=authoryear"),
    "uniquename=init", "uniquename=false")
let no_unique = latex.render_result(parse(no_unique_source, {type: "latex"}) ^ { null },
    {base_uri: base})
let no_unique_html = html_ser.to_html(no_unique.elements)
"uniquename false:"; index_of(no_unique_html, ">Smith 2020a</a>") != null

let full_unique_source = replace(replace(source, "style=numeric", "style=authoryear"),
    "uniquename=init", "uniquename=full")
let full_unique = latex.render_result(parse(full_unique_source, {type: "latex"}) ^ { null },
    {base_uri: base})
let full_unique_html = html_ser.to_html(full_unique.elements)
"uniquename full:"; index_of(full_unique_html, "John Smith 2020a") != null

let generic_limits = bib_style.settings({maxnames: "1", minnames: "1"}, "english")
let many_names = {key: "many", fields: {author: "Ada Lovelace and Grace Hopper and Alan Turing"},
    unique_name: "family"}
"generic name limits:"; bib_style.author_text(many_names, generic_limits, true) == "Lovelace et al."

let scope_ast = input("test/lambda/latex/fixtures/biblatex_scopes.tex", {type: "latex"}) ^ { null }
let scopes = latex.render_result(scope_ast, {base_uri: base})
let scope_html = html_ser.to_html(scopes.elements)
"segment resets per section:"; index_of(scope_html, "id=\"bib-s1-p1-ada1843\"") != null and
    index_of(scope_html, "id=\"bib-s2-p2-smith2020a\"") != null
"scoped labels restart:"; len(split(scope_html, ">1</a>")) >= 3
