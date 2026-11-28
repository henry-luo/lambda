import profile: lambda.doc.latex.tex_engine.profile
import native_snapshot: ._tex_native_snapshot
let kernel = input("lmd/package/doc/latex/tex_engine/kernel.tex", "text")^;
let resources = {local:{}, bundled:{}};
// The production parser now uses Script; these references come from the archived native engine.
let native = input("test/lambda/latex/fixtures/tex_generated_declarations_native.json", "json")^;

fn check(source, reference) any^ =>
    native_snapshot.run(source, kernel, {echo_definitions:true, adapters:[], resources:resources}, reference);

[for (index, source in [
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
]) check(source, native.cases[index])^];

let root = "test/lambda/latex/fixtures/local_packages/";
let path = root ++ "script-decl-main.tex";
let local = {'script-decl-base.cls':{data:input(root ++ "script-decl-base.cls", "text")^},
    'script-decl-require.sty':{data:input(root ++ "script-decl-require.sty", "text")^}};
let expanded = profile.expand_with_kernel(input(path, "text")^, kernel,
    {source_name:path, echo_definitions:true, adapters:[], resources:{local:local,bundled:{}}}, 100000)^;
native_snapshot.compare(expanded, native.local, path)^
