import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn check(source) any^ {
    let expanded = profile.expand_with_kernel(source, kernel,
        {resources: {local: {}, bundled: {}}}, 100000)^;
    let result = digest.digest(expanded)^;
    let native = parse(source, {type: "latex"})^;
    {issue: result.issue, unsupported: result.unsupported,
        matches_native: if (result.ast == null) null else result.ast == native,
        actual: if (result.ast == null or result.ast == native) null else result.ast,
        native: if (result.ast == null or result.ast == native) null else native}
}
[for (source in [
    "\\documentclass{article}\\begin{document}A\\end{document}",
    "\\documentclass {article}\\begin{document}A\\end{document}",
    "\\documentclass[12pt] {article}\\begin{document}A\\end{document}",
    "\\documentclass [12pt] {article}\\begin{document}A\\end{document}",
    "\\usepackage {xcolor} [2020/01/01]\\begin{document}A\\end{document}",
    "\\usepackage{xcolor} [2020/01/01]\\begin{document}A\\end{document}",
    "\\usepackage% comment\n{xcolor}\\begin{document}A\\end{document}",
    "\\title{A}{B}[C]\\begin{document}A\\end{document}",
    "\\title {A}\\author {B}\\date {C}\\begin{document}A\\end{document}",
    "\\title[{A} \\textbf{B}]{C}\\begin{document}A\\end{document}",
    "\\title{}\\begin{document}A\\end{document}",
    "\\title{A}% comment\n{B}\\begin{document}A\\end{document}",
    "\\documentclass\t[12pt]\t{article}\\begin{document}A\\end{document}",
    "\\usepackage[x = y]{xcolor}\\begin{document}A\\end{document}",
    "\\usepackage[foo] % comment\n {xcolor}\\begin{document}A\\end{document}",
    "\\usepackage[foo]\n{xcolor}\\begin{document}A\\end{document}",
    "\\usepackage[foo]\r\n{xcolor}\\begin{document}A\\end{document}",
    "\\usepackage[foo]  \n  {xcolor}\\begin{document}A\\end{document}",
    "\\title[Short]{A \\textbf{B}}\\begin{document}A\\end{document}",
    "\\title[Short] {Long}\\begin{document}A\\end{document}",
    "\\title{A} [Short]\\begin{document}A\\end{document}",
    "\\title% comment\n{A}\\begin{document}A\\end{document}",
    "\\title*{A}\\begin{document}A\\end{document}",
    "\\title *{A}\\begin{document}A\\end{document}",
    "\\title(A,B){A}\\begin{document}A\\end{document}",
    "\\author{A}\\date{2026}\\begin{document}A\\end{document}",
    "\\usepackage[foo]\n\n{xcolor}\\begin{document}A\\end{document}",
    "\\usepackage% a\n% b\n{xcolor}\\begin{document}A\\end{document}",
    "\\usepackage[foo]% a\n% b\n{xcolor}\\begin{document}A\\end{document}",
    "\\title[É \\emph{B}]{C}\\begin{document}A\\end{document}",
    "\\title[One% a\nTwo]{A}\\begin{document}A\\end{document}",
    "\\title{A}{B} {C}\\begin{document}A\\end{document}",
    "\\title* {A}\\begin{document}A\\end{document}"
]) check(source)^]
