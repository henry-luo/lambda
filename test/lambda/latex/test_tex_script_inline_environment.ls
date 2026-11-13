import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn check(source) any^ {
    let expanded = profile.expand_with_kernel(source, kernel, {constructor_argument_mode: true,
        echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let result = digest.digest(expanded)^;
    let native = parse(source, {type: "latex"}) ^ { null };
    {issue: result.issue, unsupported: result.unsupported, native_parsed: native != null,
        matches: if (result.ast == null or native == null) null else result.ast == native,
        actual: if (result.ast == null or native == null or result.ast == native) null else result.ast,
        native: if (result.ast == null or native == null or result.ast == native) null else native}
}
[for (source in [
    "\\begin{document}\\textbf{A\\begin{quote}B\\end{quote}C}D\\end{document}",
    "\\begin{document}\\textit{A\\begin{quote}B\\end{quote}C}\\end{document}",
    "\\begin{document}\\mystery{A\\begin{quote}B\\end{quote}C}D\\end{document}",
    "\\title{A\\begin{quote}B\\end{quote}C}\\begin{document}D\\end{document}",
    "\\begin{document}{A\\begin{quote}B\\end{quote}C}D\\end{document}",
    "\\begin{document}\\textbf{A\\begin{outer}B\\begin{inner}C\\end{inner}D\\end{outer}E}\\end{document}",
    "\\begin{document}\\textbf{\\begin{quote}\\textit{X}\\end{quote}}\\end{document}",
    "\\begin{document}A\\begin{quote}B\\end{quote}C\\end{document}",
    "\\begin{document}\\textbf{A\\begin {quote}B\\end{quote}C}\\end{document}",
    "\\begin{document}\\textbf{A\\begin{quote}[x]B\\end{quote}C}\\end{document}",
    "\\begin{document}\\textbf{A\\begin{equation}B\\end{equation}C}\\end{document}",
    "\\newenvironment{mine}{A}{B}\\begin{document}\\textbf{X\\begin{mine}Y\\end{mine}Z}\\end{document}",
    "\\begin{document}\\textbf{A\\begin{quote}B\\end{other}C}\\end{document}",
    "\\begin{document}\\textbf{A\\begin{document}B\\end{document}C}\\end{document}"
]) check(source)^]
