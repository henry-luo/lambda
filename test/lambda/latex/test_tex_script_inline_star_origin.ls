import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct

let kernel = input("lmd/package/doc/latex/tex_engine/kernel.tex", "text")^;
let options = {echo_definitions:true, adapters:[], resources:{local:{},bundled:{}}};
fn compare(source) any^ {
    let expanded = profile.expand_with_kernel(source,kernel,options,100000)^;
    let result = digest.digest(expanded)^;
    let built = reconstruct.reconstruct(expanded)^;
    let tex = parse(source,{type:"tex"})^;
    let native = parse(source,{type:"latex"})^;
    {supported:result.issue == null, text:built.text == tex.text,
        tree:result.ast == native, diagnostics:expanded.diagnostic_records == tex.diagnostics}
}

let sources = [
 "\\def\\x{\\textbf}\\begin{document}\\x*{B}\\end{document}",
 "\\def\\x{\\textbf}\\begin{document}\\x *{B}\\end{document}",
 "\\def\\x{\\textbf}\\begin{document}\\x%comment\n*{B}\\end{document}",
 "\\def\\x{\\textbf*}\\begin{document}\\x{B}\\end{document}",
 "\\def\\x{*}\\begin{document}\\textbf\\x{B}\\end{document}",
 "\\def\\x{\\textbf}\\def\\s{*}\\begin{document}\\x\\s{B}\\end{document}",
 "\\def\\x{\\textbf}\\begin{document}\\x* B\\end{document}",
 "\\def\\x{\\textbf}\\begin{document}\\x* {B}\\end{document}",
 "\\def\\x{\\textbf}\\begin{document}\\x*%comment\n{B}\\end{document}",
 "\\def\\x{\\textbf}\\begin{document}\\x * B\\end{document}",
 "\\def\\x{\\textbf }\\begin{document}\\x*{B}\\end{document}",
 "\\def\\x{\\textbf X}\\begin{document}\\x*{B}\\end{document}",
 "\\def\\x{\\textbf}\\begin{document}\\x**{B}\\end{document}",
 "\\def\\x{\\underline}\\begin{document}\\x*{猫}\\end{document}",
 "\\def\\e{}\\begin{document}\\textbf\\e*{B}\\end{document}",
 "\\def\\e{}\\begin{document}\\textbf\\e *{B}\\end{document}",
 "\\def\\e{}\\begin{document}\\textbf*\\e{B}\\end{document}",
 "\\def\\e{*}\\begin{document}\\textbf\\e {B}\\end{document}",
 "\\begin{document}\\textbf* {B}\\end{document}",
 "\\def\\e{}\\begin{document}\\textbf* \\e{B}\\end{document}",
 "\\def\\e{}\\begin{document}\\textbf* \\e {B}\\end{document}",
 "\\def\\x{\\textbf}\\def\\e{}\\begin{document}\\x* \\e{B}\\end{document}",
 "\\def\\x{\\textbf}\\def\\e{}\\begin{document}\\x* \\e {B}\\end{document}",
 "\\def\\e{}\\begin{document}\\textbf* %comment\n\\e{B}\\end{document}",
 "\\def\\e{}\\begin{document}\\textbf* \\e%comment\n{B}\\end{document}",
 "\\def\\e{}\\begin{document}\\textbf* \\e X\\end{document}",
 "\\catcode32=12\\relax\\begin{document}\\textbf* {B}\\end{document}",
 "\\catcode32=12\\relax\\def\\x{\\textbf}\\begin{document}\\x* {B}\\end{document}",
 "\\catcode32=12\\relax\\begin{document}\\textbf* X\\end{document}",
 "\\begin{document}\\textbf*{B}\\end{document}",
 "\\begin{document}\\textbf *{B}\\end{document}"
];
let results = [for (source in sources) compare(source)^];
{cases:len(results), supported:all([for (r in results) r.supported]),
 text:all([for (r in results) r.text]), tree:all([for (r in results) r.tree]),
 diagnostics:all([for (r in results) r.diagnostics])};

let root = "test/lambda/latex/fixtures/local_packages/";
let path = root ++ "script-star-main.tex";
let local = {'script-star-command.tex':{data:input(root ++ "script-star-command.tex","text")^},
    'script-star-mark.tex':{data:input(root ++ "script-star-mark.tex","text")^}};
let expanded = profile.expand_with_kernel(input(path,"text")^,kernel,
    {source_name:path,echo_definitions:true,adapters:[],resources:{local:local,bundled:{}}},100000)^;
let result = digest.digest(expanded)^;
let built = reconstruct.reconstruct(expanded)^;
let native = input(path,"latex")^;
let tex = input(path,"tex")^;
{supported:result.issue == null, text:built.text == tex.text, tree:result.ast == native,
 diagnostics:expanded.diagnostic_records == tex.diagnostics}
