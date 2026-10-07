import latex: lambda.latex.latex
import util: lambda.latex.util

fn result(options, chosen) {
    let source = "\\documentclass{article}\\usepackage[" ++ options ++ "]{natbib}" ++
        "\\begin{filecontents*}{refs.bib}\n" ++
        "@book{z,author={Zoe Smith},title={Z Book},year={2020}}\n" ++
        "@article{a,author={Alan Brown},title={A Paper},journal={Journal},year={2021}}\n" ++
        "\\end{filecontents*}\n\\begin{document}" ++
        "\\citet{z} \\citep[see][p.~4]{a} \\cite{z}\\bibliographystyle{" ++
        chosen ++ "}\\bibliography{refs}\\end{document}"
    let output = latex.render_result(parse(source, "latex") ^ { null }, null)
    {text: util.text_of(output.elements), diagnostics: [for (issue in output.diagnostics) issue.code]}
}

let author = result("authoryear,round", "plainnat")
let numeric = result("numbers,square", "unsrtnat")
let invalid = result("authoryear", "unknown")
{author: contains(author.text, "Smith (2020)") and contains(author.text, "Brown, 2021"),
 notes: contains(author.text, "p. 4"),
 numeric: contains(numeric.text, "Smith [1]") and contains(numeric.text, "[see 2, p. 4]"),
 bibliography: contains(author.text, "Z Book") and contains(numeric.text, "A Paper"),
 approximation: author.diagnostics == ["bibtex-style-approximation"],
 invalid: any([for (code in invalid.diagnostics) code == "unsupported-output"])}
