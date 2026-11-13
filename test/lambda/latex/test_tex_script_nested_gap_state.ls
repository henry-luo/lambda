import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn check(source) any^ {
    let expanded = profile.expand_with_kernel(source, kernel, {constructor_argument_mode: true,
        adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let result = digest.digest(expanded)^;
    let native = parse(source, {type: "latex"}) ^ { null };
    {issue: result.issue, unsupported: result.unsupported, native_parsed: native != null,
        matches: if (result.ast == null or native == null) null else result.ast == native,
        actual: if (result.ast == null or native == null or result.ast == native) null else result.ast,
        native: if (result.ast == null or native == null or result.ast == native) null else native}
}
[for (source in [
    "\\begin{document}\\begin{quote}\\maketitle\nA\\end{quote}TAIL\\end{document}",
    "\\begin{document}\\textbf{X\\begin{quote}\\maketitle\nA\\end{quote}TAIL}\\end{document}",
    "\\begin{document}\\begin{quote}\\title {A}B\\end{quote}TAIL\\end{document}",
    "\\begin{document}\\begin{quote}\\usepackage[x] {y}B\\end{quote}TAIL\\end{document}",
    "\\begin{document}\\begin{quote}\\maketitle\nA\\tableofcontents\nB\\end{quote}TAIL\\end{document}",
    "\\begin{document}\\begin{quote}\\begin{center}\\maketitle\nA\\end{center}B\\end{quote}TAIL\\end{document}",
    "\\begin{document}\\textbf{X\\begin{quote}\\maketitle\nA\\end{quote}TAIL}Z\\end{document}",
    "\\begin{document}\\mystery{X\\begin{quote}\\maketitle\nA\\end{quote}TAIL}{NEXT}Z\\end{document}",
    "\\title{X\\begin{quote}\\maketitle\nA\\end{quote}TAIL}\\begin{document}Z\\end{document}",
    "\\begin{document}{X\\begin{quote}\\maketitle\nA\\end{quote}TAIL}Z\\end{document}",
    "\\begin{document}\\begin{quote}\\maketitle  A\\end{quote}éλ\\end{document}",
    "\\begin{document}\\begin{quote}\\maketitle\nA\\end{quote}\\textbf{B}\\end{document}",
    "\\begin{document}\\begin{quote}\\title[A] {B}\\end{quote}TAIL\\end{document}",
    "\\begin{document}\\begin{quote}\\maketitle% comment\nA\\end{quote}TAIL\\end{document}",
    "\\begin{document}\\begin{quote}\\maketitle\nA\\end{quote}\\begin{center}B\\end{center}TAIL\\end{document}"
]) check(source)^]
