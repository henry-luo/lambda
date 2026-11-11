import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest

let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn matches(source) any^ {
    let expanded = profile.expand_with_kernel(source, kernel,
        {adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let result = digest.digest(expanded)^;
    result.issue == null and result.ast == (parse(source, {type: "latex"})^)
}
let sources = [
    "\\documentclass[11pt]{article}[2020/01/01]\\begin{document}A\\end{document}",
    "\\usepackage{unknown}[v]\\begin{document}A\\end{document}",
    "\\title{T}[v]\\author{A}[v]\\date{D}[v]\\begin{document}A\\end{document}",
    "\\title{A}[one][two]\\begin{document}A\\end{document}",
    "\\title{A}{B}[v]\\begin{document}A\\end{document}",
    "\\title{A} [v]\\begin{document}A\\end{document}",
    "\\title{A}(foo)\\begin{document}A\\end{document}",
    "\\title{A}[\\textbf{B}]\\begin{document}A\\end{document}",
    "\\title{A}{}\\begin{document}A\\end{document}",
    "\\title{A}[]\\begin{document}A\\end{document}"
];
[for (source in sources) matches(source)^]
