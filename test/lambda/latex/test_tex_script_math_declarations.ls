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
    "\\begin{document}$\\displaystyle x+1$\\end{document}",
    "\\begin{document}$\\displaystyle x+\\textstyle y+\\scriptstyle z+\\scriptscriptstyle t$\\end{document}",
    "\\begin{document}${\\displaystyle x+y}$\\end{document}",
    "\\begin{document}$$\\displaystyle\\frac{a}{b}$$\\end{document}",
    "\\begin{document}$\\operatorname{foo}(x)$\\end{document}",
    "\\begin{document}$\\operatorname*{foo}_i$\\end{document}",
    "\\begin{document}$\\operatorname{foo}$\\end{document}",
    "\\begin{document}$x(y,z)$\\end{document}",
    "\\begin{document}$A[x]$\\end{document}",
    "\\begin{document}$|x|$\\end{document}",
    "\\begin{document}$x:y$\\end{document}",
    "\\begin{document}$(x)^2$\\end{document}",
    "\\begin{document}${x(y)}$\\end{document}"
];
[for (source in sources) matches(source)^]
