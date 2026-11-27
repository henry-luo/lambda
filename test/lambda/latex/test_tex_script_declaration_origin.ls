import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct

let native_refs=input("test/lambda/latex/fixtures/tex_declaration_origin_native.json","json")^;
let options = {adapters: [], resources: {local: {}, bundled: {}}, echo_definitions: true};
let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn check(source,reference) any^ {
    let expanded = profile.expand_with_kernel(source, kernel, options, 100000)^;
    let built = reconstruct.reconstruct(expanded)^;
    let native = reference.tex;
    {expansion_issue: expanded.issue, reconstruction_issue: built.issue,
        text_match: source == reference.source and built.text == native.text, diagnostics_match: expanded.diagnostics == native.diagnostics}
}
[for (index,source in [
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
]) check(source,native_refs.cases[index])^]

let root = "test/lambda/latex/fixtures/local_packages/";
let source = input(root ++ "document.tex", "text")^;
let style = input(root ++ "lmdnotes.sty", "text")^;
let class = input(root ++ "lmdreport.cls", "text")^;
let captured = {*:options, source_name: root ++ "document.tex",
    resources: {local: {'lmdnotes.sty': {data: style}, 'lmdreport.cls': {data: class}}, bundled: {}}};
let expanded = profile.expand_with_kernel(source, kernel, captured, 100000)^;
let built = reconstruct.reconstruct(expanded)^;
let native = native_refs.local.tex;
{expansion_issue: expanded.issue, reconstruction_issue: built.issue,
    text_match: root ++ "document.tex" == native_refs.local.source and source == native_refs.local.source_text and
        style == native_refs.local.style_text and class == native_refs.local.class_text and built.text == native.text, diagnostics_match: expanded.diagnostics == native.diagnostics}
