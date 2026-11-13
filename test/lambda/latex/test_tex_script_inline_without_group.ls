import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest

let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn check(source) any^ {
    let expanded = profile.expand_with_kernel(source, kernel, {script_expl3: false,
        echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let result = digest.digest(expanded)^;
    let native = parse(source, {type: "latex"})^;
    {source: source, expansion_issue: expanded.issue, issue: result.issue,
        unsupported: result.unsupported, equal: result.ast == native,
        actual: if (result.ast == native) null else result.ast,
        native: if (result.ast == native) null else native}
}
[for (source in [
    "\\def\\x{\\textbf}\\begin{document}A\\end{document}",
    "\\def\\x{\\textit}\\begin{document}A\\end{document}",
    "\\def\\x{\\emph}\\begin{document}A\\end{document}",
    "\\def\\x{\\texttt}\\begin{document}A\\end{document}",
    "\\def\\x{\\textsf}\\begin{document}A\\end{document}",
    "\\def\\x{\\textrm}\\begin{document}A\\end{document}",
    "\\def\\x{\\underline}\\begin{document}A\\end{document}",
    "\\def\\x{A\\textbf B}\\begin{document}A\\end{document}",
    "\\def\\x{A\\textbf{B}C}\\begin{document}A\\end{document}",
    "\\def\\x{A\\textbf {B}C}\\begin{document}A\\end{document}",
    "\\def\\x{\\textbf[foo]}\\begin{document}A\\end{document}",
    "\\newcommand{\\x}{\\textbf}\\begin{document}A\\end{document}",
    "\\begin{document}A\\textbf B\\end{document}",
    "\\begin{document}A\\textbf\\end{document}",
    "\\begin{document}A\\textbf {B}C\\end{document}",
    "\\def\\x{\\textbf }\\begin{document}A\\end{document}",
    "\\def\\x{A\\textbf   B}\\begin{document}A\\end{document}",
    "\\def\\x{A\\textbf% comment\nB}\\begin{document}A\\end{document}",
    "\\def\\x{A\\textbf% comment\n{B}C}\\begin{document}A\\end{document}",
    "\\def\\x{A\\textbf\\textit B}\\begin{document}A\\end{document}",
    "\\def\\x{A{\\textbf}B}\\begin{document}A\\end{document}",
    "\\def\\x{é\\textbf 猫}\\begin{document}A\\end{document}",
    "\\def\\x{\\textbf}\\begin{document}\\x{B}\\end{document}",
    "\\begin{document}A\\textbf% comment\nB\\end{document}",
    "\\begin{document}A\\textbf[foo]B\\end{document}",
    "\\begin{document}A\\textbf\\emph{B}C\\end{document}",
    "\\def\\x{\\textbf%tail\n}\\begin{document}A\\end{document}",
    "\\def\\x{\\textbf %tail\n}\\begin{document}A\\end{document}",
    "\\def\\x{A\\textbf % comment\n{B}C}\\begin{document}A\\end{document}",
    "\\def\\x{A\\textbf\n{B}C}\\begin{document}A\\end{document}",
    "\\def\\x{{\\textbf }Z}\\begin{document}A\\end{document}",
    "\\begin{document}A\\textbf% comment\n{B}C\\end{document}",
    "\\begin{document}A\\textbf % comment\n{B}C\\end{document}",
    "\\begin{document}A\\textbf   \\end{document}",
    "\\begin{document}é\\underline 猫\\end{document}",
    "\\def\\x{\\textbf*}\\begin{document}A\\end{document}",
    "\\def\\x{A\\textbf*{B}C}\\begin{document}A\\end{document}",
    "\\def\\x{A\\textbf* B}\\begin{document}A\\end{document}",
    "\\begin{document}A\\textbf*{B}C\\end{document}",
    "\\def\\x{A\\textbf *{B}C}\\begin{document}A\\end{document}",
    "\\def\\x{A\\textbf*%c\n{B}C}\\begin{document}A\\end{document}",
    "\\def\\x{A\\textbf* }\\begin{document}A\\end{document}",
    "\\def\\x{\\textbf}\\begin{document}\\x B\\end{document}",
    "\\def\\x{\\textbf}\\begin{document}\\x.B\\end{document}",
    "\\def\\x{B}\\begin{document}\\textbf\\x\\end{document}",
    "\\begin{document}\\textbf\\char65\\end{document}",
    "\\def\\x{.}\\begin{document}\\textbf\\x\\end{document}"
]) check(source)^]

let guarded_source = "\\def\\x{\\textbf}\\begin{document}\\x*{B}\\end{document}";
let guarded_expansion = profile.expand_with_kernel(guarded_source, kernel, {script_expl3: false,
    echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
let guarded_digest = digest.digest(guarded_expansion)^;
{guarded: guarded_digest.ast == null and guarded_digest.issue == "latex-digest-unsupported" and
    guarded_digest.unsupported == "inline command star source mapping"}
