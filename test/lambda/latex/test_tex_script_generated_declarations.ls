import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel = input("lmd/package/doc/latex/tex_engine/kernel.tex", "text")^;
let resources = {local:{}, bundled:{}};

fn check(source) any^ {
    let expanded = profile.expand_with_kernel(source, kernel,
        {echo_definitions:true, adapters:[], resources:resources}, 100000)^;
    let result = digest.digest(expanded)^;
    let text = reconstruct.reconstruct(expanded)^;
    let native_text = parse(source, {type:"tex"})^;
    let native_ast = parse(source, {type:"latex"})^;
    {issue:result.issue, unsupported:result.unsupported,
        text_match:text.text == native_text.text,
        ast_match:if (result.ast == null) null else result.ast == native_ast}
}

[for (source in [
    "\\def\\n{article}\\documentclass{\\n}\\begin{document}A\\end{document}",
    "\\def\\n{xcolor}\\usepackage{\\n}\\begin{document}A\\end{document}",
    "\\def\\n{xcolor}\\usepackage*[draft]{\\n}\\begin{document}A\\end{document}",
    "\\def\\n{}\\usepackage*{\\n}\\begin{document}A\\end{document}",
    "\\def\\n{}\\usepackage*[draft]{\\n}\\begin{document}A\\end{document}",
    "\\def\\n{é}\\usepackage[É ]{\\n}\\begin{document}A\\end{document}",
    "\\def\\f{\\documentclass{article}}\\f\\begin{document}A\\end{document}",
    "\\def\\f{\\usepackage{xcolor}}\\f\\begin{document}A\\end{document}",
    "\\def\\n{B}\\title{A\\n C}\\begin{document}A\\end{document}",
    "\\def\\n{X}\\author{\\n}\\date{\\n}\\begin{document}A\\end{document}",
    "\\def\\n{xcolor}\\def\\o{draft}\\usepackage[\\o]{\\n}\\begin{document}A\\end{document}"
]) check(source)^];

let root = "test/lambda/latex/fixtures/local_packages/";
let path = root ++ "script-decl-main.tex";
let local = {'script-decl-base.cls':{data:input(root ++ "script-decl-base.cls", "text")^},
    'script-decl-require.sty':{data:input(root ++ "script-decl-require.sty", "text")^}};
let expanded = profile.expand_with_kernel(input(path, "text")^, kernel,
    {source_name:path, echo_definitions:true, adapters:[], resources:{local:local,bundled:{}}}, 100000)^;
let result = digest.digest(expanded)^;
let text = reconstruct.reconstruct(expanded)^;
let native_text = input(path, "tex")^;
let native_ast = input(path, "latex")^;
{issue:result.issue, unsupported:result.unsupported, text_match:text.text == native_text.text,
    ast_match:if (result.ast == null) null else result.ast == native_ast}
