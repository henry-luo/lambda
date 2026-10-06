// Reference text captured with BibLaTeX 3.20 and Biber 2.20; see proposal §7.3.
import latex: lambda.latex.latex
import html_ser: lambda.latex.to_html

let source = input("test/lambda/latex/fixtures/biblatex_reference_320.tex", "text") ^ { "" }
let base = "test/lambda/latex/fixtures"
let cases = [
    {profile: "numeric", citation: [
        ">1</a>, <a class=\"latex-cite\" href=\"#bib-smith2020a\">2</a>",
        "[see <a class=\"latex-cite\" href=\"#bib-ada1844\">4</a>",
        ">Lovelace [1]</a>"],
     bibliography: ["Ada Lovelace. Notes on the Analytical Engine. Example Press, 1843.",
        "John Smith. “First Result”. In: Math Notes (2020)."]},
    {profile: "numeric-comp", citation: [
        ">1</a>–<a class=\"latex-cite\" href=\"#bib-smith2020b\">3</a>",
        ">Lovelace [1]</a>"],
     bibliography: ["Ada Lovelace. Notes on the Analytical Engine. Example Press, 1843.",
        "Jane Smith. “Second Result”. In: Science (2020)."]},
    {profile: "authoryear", citation: [
        ">Lovelace 1843</a>; <a class=\"latex-cite\" href=\"#bib-smith2020a\">Smith 2020a</a>",
        ">Lovelace (1843)</a>",
        ">Lovelace 1843</a>; <a class=\"latex-cite\" href=\"#bib-ada1844\">Lovelace 1844</a>"],
     bibliography: ["Lovelace, Ada (1843). Notes on the Analytical Engine. Example Press.",
        "Smith, Jane (2020b). “Second Result”. In: Science."]},
    {profile: "authoryear-comp", citation: [
        ">Smith 2020a</a>; <a class=\"latex-cite\" href=\"#bib-smith2020b\">Smith 2020b</a>",
        ">Lovelace 1843</a>, <a class=\"latex-cite\" href=\"#bib-ada1844\">1844</a>"],
     bibliography: ["Lovelace, Ada (1843). Notes on the Analytical Engine. Example Press.",
        "Smith, John (2020a). “First Result”. In: Math Notes."]},
    {profile: "alphabetic", citation: [
        ">Lov43</a>; <a class=\"latex-cite\" href=\"#bib-smith2020a\">Smi20a</a>",
        ">Lovelace [Lov43]</a>",
        ">Lov43</a>; <a class=\"latex-cite\" href=\"#bib-ada1844\">Lov44</a>"],
     bibliography: ["[Lov43] </span>Ada Lovelace. Notes on the Analytical Engine.",
        "John Smith. “First Result”. In: Math Notes (2020)."]},
    {profile: "authortitle", citation: [
        ">Lovelace, Notes on the Analytical Engine</a>",
        ">Smith, “First Result”</a>",
        ">Lovelace (Notes on the Analytical Engine)</a>"],
     bibliography: ["Lovelace, Ada. Notes on the Analytical Engine. Example Press, 1843.",
        "Smith, Jane. “Second Result”. In: Science (2020)."]}
]

for (case in cases) {
    let ast = parse(replace(source, "style=numeric", "style=" ++ case.profile),
        {type: "latex"}) ^ { null }
    let result = latex.render_result(ast, {base_uri: base})
    let html = html_ser.to_html(result.elements)
    case.profile; [for (issue in result.diagnostics) issue.code]
    "citations:"; [for (snippet in case.citation) index_of(html, snippet) != null]
    "bibliography:"; [for (snippet in case.bibliography) index_of(html, snippet) != null]
    "links:"; all([for (key in ["ada1843", "ada1844", "smith2020a", "smith2020b"])
        index_of(html, "id=\"bib-" ++ key ++ "\"") != null]) and
        all([for (key in ["ada1843", "ada1844", "smith2020b"])
            index_of(html, "href=\"#bib-" ++ key ++ "\"") != null])
    "note tie:"; index_of(html, "p. 12") != null
}
