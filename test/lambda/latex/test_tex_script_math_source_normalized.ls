import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
fn check(source) any^ {
 let ex=profile.expand_with_kernel(source,kernel,options,100000)^;
 let result=digest.digest(ex)^; let built=reconstruct.reconstruct(ex)^;
 let tex=parse(source,{type:"tex"})^; let native=parse(source,{type:"latex"})^;
 {supported:result.issue==null,text:built.text==tex.text,
  diagnostics:ex.diagnostic_records==tex.diagnostics,tree:result.ast==native}
}
let sources = [
 "\\def\\m{$x^^2$}\\begin{document}A\\end{document}",
 "\\begin{document}$x^^2$\\end{document}",
 "\\begin{document}$x^^41$\\end{document}",
 "\\begin{document}$^^61^2$\\end{document}",
 "\\begin{document}$x^^5e2$\\end{document}",
 "\\begin{document}$x^^5f2$\\end{document}",
 "\\begin{document}$x^^20+y$\\end{document}",
 "\\begin{document}$^^5calpha^2$\\end{document}",
 "\\begin{document}$x^2^^5e3$\\end{document}",
 "\\begin{document}$$x^^41$$\\end{document}",
 "\\begin{document}\\(x^^41\\)\\end{document}",
 "é\\begin{document}$x^^41$\\end{document}",
 "\\begin{document}${^^61+b}^2$\\end{document}",
 "\\begin{document}$x^^41^^42$\\end{document}",
 "\\def\\m{^^61}\\begin{document}$\\m^2$\\end{document}",
 "\\def\\m{^^5e2}\\begin{document}$x\\m$\\end{document}",
 "\\def\\m{X^^61Y}\\begin{document}$\\m$\\end{document}",
 "\\gdef\\m{^^61}\\begin{document}$\\m$\\end{document}",
 "\\edef\\m{^^61}\\begin{document}$\\m$\\end{document}",
 "\\newcommand{\\m}{^^61}\\begin{document}$\\m$\\end{document}",
 "\\def\\m{A^^7bB^^7dC}\\begin{document}$\\m$\\end{document}",
 "\\def\\m{^^5calpha}\\begin{document}$\\m$\\end{document}",
 "\\begin{document}$x^2+y_i^3$\\end{document}",
 "\\begin{document}$a < b + c$\\end{document}",
 "\\begin{document}A $\\frac{1}{2}+\\alpha$ B\\end{document}"
];
let results=[for(source in sources) check(source)^];
{cases:len(results),supported:all([for(r in results) r.supported]),
 text:all([for(r in results) r.text]),diagnostics:all([for(r in results) r.diagnostics]),
 tree:all([for(r in results) r.tree])};
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "script-math-source-main.tex";
let child="script-math-source-child.tex";
let expanded=profile.expand_with_kernel(input(path,"text")^,kernel,
 {source_name:path,echo_definitions:true,adapters:[],resources:{local:{[child]:{data:input(root ++ child,"text")^}},bundled:{}}},100000)^;
let result=digest.digest(expanded)^; let built=reconstruct.reconstruct(expanded)^;
let native=input(path,"latex")^; let tex=input(path,"tex")^;
{supported:result.issue==null,text:built.text==tex.text,
 diagnostics:expanded.diagnostic_records==tex.diagnostics,tree:result.ast==native}
