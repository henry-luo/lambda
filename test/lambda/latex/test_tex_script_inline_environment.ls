import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import native_snapshot: ._tex_native_snapshot
let native_refs=input("test/lambda/latex/fixtures/tex_inline_environment_native.json","json")^;
let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn check(source,reference) any^ {
    let expanded = profile.expand_with_kernel(source, kernel, {constructor_argument_mode: true,
        echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let result = digest.digest(expanded)^;
    let native = reference.latex;
    let same = if(result.ast == null or native == null) null else native_snapshot.tree_matches(result.ast,reference,source)^;
    {issue: if(source==reference.source) result.issue else "snapshot-source-mismatch", unsupported: result.unsupported, native_parsed: native != null,
        matches: same,
        actual: if (result.ast == null or native == null or same == true) null else result.ast,
        native: if (result.ast == null or native == null or same == true) null else native}
}
[for (index,source in [
    "\\begin{document}\\textbf{A\\begin{quote}B\\end{quote}C}D\\end{document}",
    "\\begin{document}\\textit{A\\begin{quote}B\\end{quote}C}\\end{document}",
    "\\begin{document}\\mystery{A\\begin{quote}B\\end{quote}C}D\\end{document}",
    "\\title{A\\begin{quote}B\\end{quote}C}\\begin{document}D\\end{document}",
    "\\begin{document}{A\\begin{quote}B\\end{quote}C}D\\end{document}",
    "\\begin{document}\\textbf{A\\begin{outer}B\\begin{inner}C\\end{inner}D\\end{outer}E}\\end{document}",
    "\\begin{document}\\textbf{\\begin{quote}\\textit{X}\\end{quote}}\\end{document}",
    "\\begin{document}A\\begin{quote}B\\end{quote}C\\end{document}",
    "\\begin{document}\\textbf{A\\begin {quote}B\\end{quote}C}\\end{document}",
    "\\begin{document}\\textbf{A\\begin  {quote}B\\end {quote}C}\\end{document}",
    "\\begin{document}\\mystery{A\\begin\t{quote}B\\end{quote}C}\\end{document}",
    "\\begin{document}\\mystery{A\\begin \n\n{quote}B\\end{quote}C}\\end{document}",
    "\\begin{document}\\mystery{A\\begin%comment\n{quote}B\\end{quote}C}\\end{document}",
    "\\begin{document}\\textbf{A\\begin{quote}[x]B\\end{quote}C}\\end{document}",
    "\\begin{document}\\textbf{A\\begin{equation}B\\end{equation}C}\\end{document}",
    "\\newenvironment{mine}{A}{B}\\begin{document}\\textbf{X\\begin{mine}Y\\end{mine}Z}\\end{document}",
    "\\begin{document}\\textbf{A\\begin{quote}B\\end{other}C}\\end{document}",
    "\\begin{document}\\textbf{A\\begin{document}B\\end{document}C}\\end{document}"
]) check(source,native_refs.cases[index])^]
