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
 "\\begin{document}$x__2$\\end{document}",
 "\\begin{document}$x^2^3$\\end{document}",
 "\\begin{document}$x_1_2$\\end{document}",
 "\\begin{document}$x^a^b^c$\\end{document}",
 "\\begin{document}$x_{ab}_{cd}^e^f$\\end{document}",
 "\\begin{document}$x^{a}^{bc}$\\end{document}",
 "\\begin{document}$x^2_3^4_5$\\end{document}",
 "\\begin{document}$x^a^\\alpha$\\end{document}",
 "\\begin{document}$x_1^2_3^4$\\end{document}",
 "\\begin{document}$x^\\alpha^\\beta$\\end{document}",
 "\\begin{document}$x^{12}^3$\\end{document}",
 "\\begin{document}$x^a ^b$\\end{document}",
 "\\begin{document}$x_1 _2$\\end{document}",
 "\\begin{document}$\\alpha^2^3$\\end{document}",
 "\\begin{document}$\\frac12^a^b$\\end{document}",
 "\\begin{document}$x^\\frac12^z$\\end{document}",
 "\\begin{document}$x^{a_1}^b$\\end{document}",
 "\\begin{document}$x_{a^2}_b$\\end{document}",
 "\\begin{document}$$x^a^b$$\\end{document}",
 "\\begin{document}\\(x^2^3\\)\\end{document}",
 "\\def\\m{^2^3}\\begin{document}$x\\m$\\end{document}",
 "\\def\\a{2}\\begin{document}$x^\\a^3$\\end{document}",
 "\\def\\b{3}\\begin{document}$x^2^\\b$\\end{document}",
 "\\def\\x{x}\\begin{document}$\\x^2^3$\\end{document}"
];
let results=[for(source in sources) check(source)^];
{cases:len(results),supported:all([for(r in results) r.supported]),
 text:all([for(r in results) r.text]),diagnostics:all([for(r in results) r.diagnostics]),
 tree:all([for(r in results) r.tree])};
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "script-repeated-math-main.tex";
let child="script-repeated-math-child.tex";
let expanded=profile.expand_with_kernel(input(path,"text")^,kernel,
 {source_name:path,echo_definitions:true,adapters:[],resources:{local:{[child]:{data:input(root ++ child,"text")^}},bundled:{}}},100000)^;
let result=digest.digest(expanded)^; let built=reconstruct.reconstruct(expanded)^;
let native=input(path,"latex")^; let tex=input(path,"tex")^;
{supported:result.issue==null,text:built.text==tex.text,
 diagnostics:expanded.diagnostic_records==tex.diagnostics,tree:result.ast==native}
