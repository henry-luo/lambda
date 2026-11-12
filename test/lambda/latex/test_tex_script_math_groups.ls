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
    "\\begin{document}${a+b}$\\end{document}",
    "\\begin{document}${a}^2$\\end{document}",
    "\\begin{document}${}$\\end{document}",
    "\\begin{document}${x_{i}}$\\end{document}",
    "\\begin{document}$a{b}c$\\end{document}",
    "\\begin{document}${12}^3$\\end{document}",
    "\\begin{document}$\\sqrt{{a}}$\\end{document}",
    "\\begin{document}$${a+b}$$\\end{document}"
];
[for (source in sources) matches(source)^]
