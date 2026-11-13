import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct

let options = {adapters: [], resources: {local: {}, bundled: {}}, echo_definitions: true};
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn check(source) any^ {
    let expanded = profile.expand_with_kernel(source, kernel, options, 100000)^;
    let built = reconstruct.reconstruct(expanded)^;
    let native = parse(source, {type: "tex"})^;
    {expansion_issue: expanded.issue, reconstruction_issue: built.issue,
        text_match: built.text == native.text, diagnostics_match: expanded.diagnostics == native.diagnostics}
}
[for (source in [
    "\\documentclass{article}\n\\usepackage{xcolor}\n\\begin{document}A\\end{document}",
    "\\documentclass{article}\n\n\\usepackage{xcolor}\n\n\\begin{document}A\\end{document}",
    "\\documentclass [12pt] {article} [2020/01/01]\n\\begin{document}A\\end{document}",
    "\\documentclass{article}% class comment\n\\usepackage{xcolor}% package comment\n\\begin{document}A\\end{document}",
    "\\usepackage {xcolor} [2020/01/01]\n\\begin{document}A\\end{document}",
    "\\usepackage[foo]{xcolor,geometry}\n\\begin{document}A\\end{document}",
    "\\def\\p{xcolor}\\usepackage{\\p}\n\\begin{document}A\\end{document}",
    "\\def\\c{article}\\documentclass{\\c}\n\\begin{document}A\\end{document}",
    "\\def\\p{\\usepackage{xcolor}}\\p\n\\begin{document}A\\end{document}",
    "\\def\\c{\\documentclass{article}}\\c\n\\begin{document}A\\end{document}",
    "\\LoadClass[12pt]{article}\n\\begin{document}A\\end{document}",
    "\\def\\c{article}\\LoadClass{\\c}\n\\begin{document}A\\end{document}",
    "\\RequirePackage[foo]{xcolor}\n\\begin{document}A\\end{document}",
    "\\def\\p{xcolor}\\RequirePackage{\\p}\n\\begin{document}A\\end{document}",
    "\\documentclass{article}\\usepackage{xcolor}\\usepackage{xcolor}\n\\begin{document}A\\end{document}"
]) check(source)^]

let root = "test/lambda/latex/fixtures/local_packages/";
let source = input(root ++ "document.tex", "text")^;
let style = input(root ++ "lmdnotes.sty", "text")^;
let class = input(root ++ "lmdreport.cls", "text")^;
let captured = {*:options, source_name: root ++ "document.tex",
    resources: {local: {'lmdnotes.sty': {data: style}, 'lmdreport.cls': {data: class}}, bundled: {}}};
let expanded = profile.expand_with_kernel(source, kernel, captured, 100000)^;
let built = reconstruct.reconstruct(expanded)^;
let native = input(root ++ "document.tex", "tex")^;
{expansion_issue: expanded.issue, reconstruction_issue: built.issue,
    text_match: built.text == native.text, diagnostics_match: expanded.diagnostics == native.diagnostics}
