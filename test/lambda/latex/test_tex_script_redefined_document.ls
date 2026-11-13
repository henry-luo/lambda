import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn check(source) any^ {
    let expanded = profile.expand_with_kernel(source, kernel,
        {echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let built = reconstruct.reconstruct(expanded)^;
    let native = parse(source, {type: "tex"})^;
    {issue: expanded.issue, reconstruction_issue: built.issue,
        text_match: built.text == native.text, diagnostics_match: expanded.diagnostics == native.diagnostics,
        actual: if (built.text == native.text) null else built.text,
        native: if (built.text == native.text) null else native.text}
}
[for (source in [
    "\\def\\setcounter#1#2{Q}\\begin{document}\\setcounter{foo}{7}\\end{document}",
    "\\def\\addtocounter#1#2{Q}\\begin{document}\\addtocounter{foo}{7}\\end{document}",
    "\\def\\stepcounter#1{Q}\\begin{document}\\stepcounter{foo}\\end{document}",
    "\\def\\setlength#1#2{Q}\\begin{document}\\setlength{\\foo}{7pt}\\end{document}",
    "\\def\\addtolength#1#2{Q}\\begin{document}\\addtolength{\\foo}{7pt}\\end{document}",
    "\\def\\setcounter#1#2{\\global\\count0=#2 }\\count0=0\\begin{document}\\setcounter{foo}{7}\\the\\count0\\end{document}",
    "\\def\\stepcounter#1{\\global\\advance\\count0 by 1 }\\count0=0\\begin{document}\\stepcounter{foo}\\the\\count0\\end{document}",
    "\\def\\setcounter#1#2{(#1/#2)}\\begin{document}\\setcounter {foo} {7}\\end{document}",
    "\\def\\setcounter#1#2{(#1/#2)}\\begin{document}\\setcounter% note\n{foo}% x\n{7}\\end{document}",
    "\\def\\setcounter#1#2{(#1/#2)}\\begin{document}\\setcounter ab\\end{document}",
    "\\def\\setcounter#1#2{(#1/#2)}\\def\\value{X}\\begin{document}\\setcounter{foo}{\\value}\\end{document}",
    "\\def\\setcounter#1#2{Q}\\def\\call{\\setcounter{foo}{7}}\\begin{document}\\call\\end{document}",
    "\\def\\setcounter#1#2{Q}\\begin{document}\\noexpand\\setcounter{foo}{7}\\end{document}",
    "\\def\\setcounter#1#2{Q}\\begin{document}\\setcounter{foo}{7}\\setcounter{bar}{8}\\end{document}",
    "\\def\\setcounter#1#2{Q}\\begin{document}$\\setcounter{foo}{7}$\\end{document}"
]) check(source)^]

let root = "test/lambda/latex/fixtures/local_packages/";
let source = input(root ++ "redefined_call_main.tex", "text")^;
let package = input(root ++ "redefined_call.sty", "text")^;
let expanded = profile.expand_with_kernel(source, kernel, {echo_definitions: true, adapters: [],
    source_name: root ++ "redefined_call_main.tex", resources: {local: {'redefined_call.sty': {data: package}}, bundled: {}}}, 100000)^;
let built = reconstruct.reconstruct(expanded)^;
let native = input(root ++ "redefined_call_main.tex", "tex")^;
{issue: expanded.issue, reconstruction_issue: built.issue,
    text_match: built.text == native.text, diagnostics_match: expanded.diagnostics == native.diagnostics,
    actual: if (built.text == native.text) null else built.text,
    native: if (built.text == native.text) null else native.text}
