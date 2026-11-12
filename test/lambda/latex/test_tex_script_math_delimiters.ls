import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn result(source) any^ {
    let expanded = profile.expand_with_kernel(source, kernel,
        {echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    digest.digest(expanded)
}
fn matches(source) any^ {
    let parsed = result(source)^;
    parsed.issue == null and parsed.ast == (parse(source, {type: "latex"})^)
}
let sources = [
    "\\begin{document}$\\left(x+1\\right)$\\end{document}",
    "\\begin{document}$\\left.\\frac{a}{b}\\right|$\\end{document}",
    "\\begin{document}$\\left\\langle x\\right\\rangle$\\end{document}",
    "\\begin{document}$\\left(\\left[x\\right]+y\\right)$\\end{document}",
    "\\begin{document}$\\left(x\\right)^2$\\end{document}",
    "\\begin{document}$\\left(x\\middle|y\\right)$\\end{document}",
    "\\begin{document}$\\left(x$\\end{document}",
    "\\begin{document}$\\left(a\\middle|b\\middle|c\\right)$\\end{document}",
    "\\begin{document}$\\left(\\left[x\\middle|y\\right]\\middle|z\\right)$\\end{document}",
    "\\begin{document}$\\left(\\frac{a}{\\left[b\\right]}\\right)$\\end{document}",
    "\\begin{document}$\\left\\{x\\right\\}$\\end{document}",
    "\\begin{document}$\\left(x+1\\right.$\\end{document}",
    "\\begin{document}$\\left(x\\right)_{i}$\\end{document}",
    "\\begin{document}$\\left(\\right)$\\end{document}",
    "\\begin{document}$\\left(\\left[x\\right]$\\end{document}",
    "\\begin{document}$\\left(x\\right$\\end{document}",
    "\\begin{document}$\\left(x\\right{)}$\\end{document}",
    "\\begin{document}$\\left(x\\right{}$\\end{document}",
    "\\begin{document}$\\left(x\\right{\\rangle}$\\end{document}",
    "\\begin{document}$\\left(x\\right{ )}$\\end{document}"
];
let invalid = result("\\begin{document}$\\left(x\\middle a\\right)$\\end{document}")^;
[*[for (source in sources) matches(source)^],
    invalid.issue == "latex-digest-unsupported" and invalid.unsupported == "math delimiter"]
