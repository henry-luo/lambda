import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import native_snapshot: ._tex_native_snapshot
let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
// Preserve native reference trees after the production input route moved to Script.
let native = input("test/lambda/latex/fixtures/tex_declaration_digest_native.json", "json")^;
fn check(source, reference) any^ {
    let expanded = profile.expand_with_kernel(source, kernel,
        {resources: {local: {}, bundled: {}}}, 100000)^;
    let result = digest.digest(expanded)^;
    let same = if (result.ast == null) source == reference.source
        else native_snapshot.tree_matches(result.ast, reference, source)^;
    {issue: if (source == reference.source) result.issue else "snapshot-source-mismatch", unsupported: result.unsupported,
        matches_native: if (result.ast == null) null else same,
        actual: if (result.ast == null or same) null else result.ast,
        native: if (result.ast == null or same) null else reference.latex}
}
[for (index, source in [
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
]) check(source, native.cases[index])^]
