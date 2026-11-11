import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn result(source) any^ {
    let expanded = profile.expand_with_kernel(source, kernel,
        {adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    digest.digest(expanded)
}
fn matches(source) any^ {
    let parsed = result(source)^;
    parsed.issue == null and parsed.ast == (parse(source, {type: "latex"})^)
}
let sources = [
    "\\begin {document}A\\end {document}",
    "\\begin\t{document}A\\end\t{document}",
    "\\begin\n{document}A\\end\n{document}",
    "\\begin{document}\\begin {quote}A\\end {quote}B\\end{document}",
    "\\begin{document}A\\end{document}"
];
let unclosed = result("\\begin{document}A\\end {document}")^;
[*[for (source in sources) matches(source)^],
    unclosed.issue == "latex-digest-unsupported" and unclosed.unsupported == "unclosed environment"]
