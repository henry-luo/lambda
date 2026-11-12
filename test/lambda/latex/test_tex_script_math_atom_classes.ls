import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn matches(source) any^ {
    let expanded = profile.expand_with_kernel(source, kernel,
        {echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let parsed = digest.digest(expanded)^;
    parsed.issue == null and parsed.ast == (parse(source, {type: "latex"})^)
}
let sources = [
    "\\begin{document}$\\mathord{x}$\\end{document}",
    "\\begin{document}$\\mathbin{+}\\mathrel{=}\\mathopen{(}\\mathclose{)}\\mathpunct{,}\\mathinner{x}$\\end{document}",
    "\\begin{document}$\\mathop{abc}_i$\\end{document}",
    "\\begin{document}$\\mathchoice{a}{b}{c}{d}$\\end{document}",
    "\\begin{document}$\\mathchoice{a+b}{\\frac{x}{y}}{}{z}$\\end{document}",
    "\\begin{document}$\\mathord x_i$\\end{document}",
    "\\begin{document}$\\mathchoice abcd$\\end{document}",
    "\\begin{document}$\\mathchoice{a}{b}{c}{d}^2$\\end{document}",
    "\\begin{document}$\\mathord{\\mathbin{+}}$\\end{document}"
];
[for (source in sources) matches(source)^]
