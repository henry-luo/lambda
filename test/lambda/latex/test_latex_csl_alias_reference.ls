// Admission fixtures for the CSL style aliases (Lambda_Design_BibTeX_CSL §7.4):
// each alias document is compared with the text that biblatex itself produced.
// Reference text: biblatex 3.21, biber 2.21, biblatex-apa 9.20, biblatex-ieee 1.4f,
// biblatex-chicago 3.18b on TeX Live 2025 (test/latex/reference/csl_alias/).
// Every item prints "same" or the reference and Lambda texts, so this golden is
// the record of the differences explained in the reference README.
import latex: lambda.latex.latex
import util: lambda.latex.util

let dir = "test/lambda/latex/fixtures/csl_alias/"

let references = [
    {name: "apa",
     citations: "First (Doe & Roe, 2020). Second (Roe, 2021). Third (Smith et al., 2019).",
     entries: ["Doe, J., & Roe, J. (2020). Measuring citation styles. Journal of Examples, 12 (3), 1–9. https://doi.org/10.1234/jex.2020.003",
               "Roe, J. (2021). A book about bibliographies. Example Press.",
               "Smith, A., Jones, B., & Lee, C. (2019). Three authors agree. Review of Samples, 4, 20–31."],
     notes: []},
    {name: "ieee",
     citations: "First [1]. Second [2]. Third [3].",
     entries: ["[1] J. Doe and J. Roe, “Measuring citation styles,” Journal of Examples, vol. 12, no. 3, pp. 1–9, 2020. doi: 10.1234/jex.2020.003",
               "[2] J. Roe, A Book about Bibliographies. Boston: Example Press, 2021.",
               "[3] A. Smith, B. Jones, and C. Lee, “Three authors agree,” Review of Samples, vol. 4, pp. 20–31, 2019."],
     notes: []},
    {name: "chicago_authordate",
     citations: "First (Doe and Roe 2020). Second (Roe 2021). Third (Smith, Jones, and Lee 2019).",
     entries: ["Doe, John, and Jane Roe. 2020. “Measuring Citation Styles.” Journal of Examples 12 (3): 1–9. https://doi.org/10.1234/jex.2020.003.",
               "Roe, Jane. 2021. A Book about Bibliographies. Boston: Example Press.",
               "Smith, Alice, Bob Jones, and Carol Lee. 2019. “Three Authors Agree.” Review of Samples 4:20–31."],
     notes: []},
    {name: "chicago_notes",
     citations: "First.1 Second.2 Third.3",
     entries: ["Doe, John, and Jane Roe. “Measuring Citation Styles.” Journal of Examples 12, no. 3 (2020): 1–9. https://doi.org/10.1234/jex.2020.003.",
               "Roe, Jane. A Book about Bibliographies. Boston: Example Press, 2021.",
               "Smith, Alice, Bob Jones, and Carol Lee. “Three Authors Agree.” Review of Samples 4 (2019): 20–31."],
     notes: ["John Doe and Jane Roe, “Measuring Citation Styles,” Journal of Examples 12, no. 3 (2020): 1–9, https://doi.org/10.1234/jex.2020.003.",
             "Jane Roe, A Book about Bibliographies (Boston: Example Press, 2021).",
             "Alice Smith, Bob Jones, and Carol Lee, “Three Authors Agree,” Review of Samples 4 (2019): 20–31."]}
]

// collapse runs of whitespace and drop the footnote back-link arrow
fn normalized(text) => trim(replace(replace(replace(text, "↩", ""), "\n", " "), "  ", " "))

fn has_class(node, wanted) => node is element and node.class != null and
    (contains(" " ++ string(node.class) ++ " ", " " ++ wanted ++ " ") ^ { false })

// rendered elements may hold their children in nested arrays
fn items_with_class(node, wanted) {
    if (node is array or node is list) [for (child in node, found in items_with_class(child, wanted)) found]
    else if (not (node is element)) []
    else if (has_class(node, wanted)) [node]
    else [for (child in node, found in items_with_class(child, wanted)) found]
}

fn footnote_items(node) {
    if (node is array or node is list) [for (child in node, found in footnote_items(child)) found]
    else if (not (node is element)) []
    else if (has_class(node, "latex-footnotes"))
        [for (child in node, item in items_named(child, "li")) item]
    else [for (child in node, found in footnote_items(child)) found]
}

fn items_named(node, tag) {
    if (node is array or node is list) [for (child in node, found in items_named(child, tag)) found]
    else if (not (node is element)) []
    else if (string(name(node)) == tag) [node]
    else [for (child in node, found in items_named(child, tag)) found]
}

fn compare(reference, lambda_text) =>
    if (reference == lambda_text) "same" else {reference: reference, lambda: lambda_text}

fn check(ref) {
    let path = dir ++ ref.name ++ ".tex"
    let result = latex.render_result(latex.parse_file(path), {source_path: path})
    let body = util.text_of(result.body)
    let citations = normalized(slice(body, index_of(body, "First"), index_of(body, "References")))
    let entries = [for (item in items_with_class(result.elements, "csl-entry")) normalized(util.text_of(item))]
    let notes = [for (item in footnote_items(result.elements)) normalized(util.text_of(item))];
    {name: ref.name,
     diagnostics: [for (issue in result.diagnostics) issue.code],
     citations: compare(ref.citations, citations),
     entries: [for (i in 0 to len(ref.entries) - 1) compare(ref.entries[i], entries[i])],
     notes: [for (i in 0 to len(ref.notes) - 1) compare(ref.notes[i], notes[i])]}
}

[for (ref in references) check(ref)]
