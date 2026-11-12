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
    "\\def\\close{\\end{quote}}\\begin{document}\\begin{quote}A\\close\\end{document}",
    "\\def\\finish{\\end{document}}\\begin{document}A\\finish tail",
    "\\def\\x{A\\end{x}B}\\begin{document}C\\end{document}",
    "\\def\\x{A\\end  {x}B}\\begin{document}C\\end{document}",
    "\\def\\x{A\\end!B}\\begin{document}C\\end{document}",
    "\\def\\x{\\textbf{A\\end{x}B}}\\begin{document}C\\end{document}",
    "\\def\\x{\\end{x}\\end{y}}\\begin{document}C\\end{document}"
];
[for (source in sources) matches(source)^]
