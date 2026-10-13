// Guide-level biblatex styles reach their CSL style without a render option, and
// the substitution is reported (Lambda_Design_BibTeX_CSL §7.4).
import latex: lambda.latex.latex
import util: lambda.latex.util

let path = "test/lambda/latex/fixtures/citeproc/document.tex"
let source = input(path, "text")^

fn render(declaration, options = null) =>
    latex.render_result(latex.parse_source(
        replace(source, "\\usepackage{biblatex}", declaration), path), options)

// the visible text of the first parenthetical citation sentence
fn first_citation(result) {
    let text = util.text_of(result.body)
    let at = index_of(text, "Parenthetical")
    if (at == null) null else trim(slice(text, at, index_of(text, "Textual")))
}

fn summary(result) => ({
    processor: if (result.metadata.bibliography == null) null
        else result.metadata.bibliography.processor,
    biblatex_style: if (result.metadata.bibliography == null) null
        else result.metadata.bibliography.biblatex_style,
    codes: [for (issue in result.diagnostics) issue.code],
    citation: first_citation(result)
})

let cases = [
    ["apa", "\\usepackage[style=apa]{biblatex}", null],
    ["ieee", "\\usepackage[style=ieee]{biblatex}", null],
    ["chicago authordate", "\\usepackage[authordate]{biblatex-chicago}", null],
    ["chicago notes", "\\usepackage{biblatex-chicago}", null],
    ["ignored options", "\\usepackage[style=apa,maxnames=3,sorting=nyt]{biblatex}\\renewbibmacro{in:}{}", null],
    // an explicit render option keeps priority; a biblatex style beside it conflicts
    ["explicit citeproc conflicts", "\\usepackage[style=apa]{biblatex}", {citeproc: {style: "ieee"}}],
    ["split style", "\\usepackage[citestyle=apa,bibstyle=ieee]{biblatex}", null],
    ["standard style", "\\usepackage[style=numeric]{biblatex}", null],
    ["unmapped style", "\\usepackage[style=mla]{biblatex}", null],
    ["both packages", "\\usepackage{biblatex}\\usepackage{biblatex-chicago}", null]
];

[for (entry in cases) [entry[0], summary(render(entry[1], entry[2]))]]
