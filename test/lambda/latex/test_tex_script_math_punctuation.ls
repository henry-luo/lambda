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
 "\\def\\e{}\\begin{document}$x_<\\e=2$\\end{document}",
 "\\def\\e{}\\begin{document}$x_-\\e>2$\\end{document}",
 "\\def\\e{}\\begin{document}$x_;\\e;2$\\end{document}",
 "\\begin{document}$x_<%comment\n=2$\\end{document}",
 "\\begin{document}$x_- %comment\n>2$\\end{document}",
 "\\def\\m{<}\\def\\n{=}\\begin{document}$x_\\m\\n 2$\\end{document}",
 "\\catcode`!=13\\def!{<}\\begin{document}$x_!=2$\\end{document}",
 "\\begin{document}$x__2$\\end{document}",
 "\\begin{document}$x_+2$\\end{document}",
 "\\begin{document}$x_-2$\\end{document}",
 "\\begin{document}$x_<2$\\end{document}",
 "\\begin{document}$x_<=2$\\end{document}",
 "\\begin{document}$x_!=2$\\end{document}",
 "\\begin{document}$x_!=a$\\end{document}",
 "\\begin{document}$x_- >2$\\end{document}",
 "\\begin{document}$x_->2$\\end{document}",
 "\\begin{document}$x_;;2$\\end{document}",
 "\\begin{document}$x_&2$\\end{document}",
 "\\begin{document}$x_(a)$\\end{document}",
 "\\begin{document}$x_[a]$\\end{document}",
 "\\begin{document}$x_^2$\\end{document}",
 "\\def\\m{$x^^2$}\\begin{document}A\\end{document}",
 "\\begin{document}$x_.$\\end{document}",
 "\\begin{document}$x_...$\\end{document}",
 "\\begin{document}$x_< =2$\\end{document}",
 "\\begin{document}$a+b-c*d/e=f$\\end{document}",
 "\\begin{document}$a<b<=c>d>=e$\\end{document}",
 "\\begin{document}$a->b$\\end{document}",
 "\\begin{document}$a- >b$\\end{document}",
 "\\begin{document}$a->^2b$\\end{document}",
 "\\begin{document}$a+^2b$\\end{document}",
 "\\begin{document}$a=^2b$\\end{document}",
 "\\begin{document}$a<&b;;c$\\end{document}",
 "\\begin{document}$a!b!=c$\\end{document}",
 "\\begin{document}$x_+^2$\\end{document}",
 "\\begin{document}$x_<_2$\\end{document}",
 "\\begin{document}$x_^2$\\end{document}",
 "\\begin{document}$_1+^2$\\end{document}",
 "\\begin{document}$x_<=2^3$\\end{document}",
 "\\begin{document}$x_!=2_3$\\end{document}",
 "\\begin{document}$x_-\\alpha$\\end{document}",
 "\\begin{document}$$x_+2$$\\end{document}",
 "\\begin{document}\\(x_!=a\\)\\end{document}",
 "\\def\\m{<=}\\begin{document}$x_\\m 2$\\end{document}",
 "\\def\\m{!}\\begin{document}$x_\\m=2$\\end{document}",
 "\\def\\m{>}\\begin{document}$x_-\\m 2$\\end{document}",
 "\\begin{document}$x_; ;2$\\end{document}",
 "\\begin{document}$x_\\alpha^2$\\end{document}",
 "\\begin{document}$x_{a+b}^2$\\end{document}"
];
let results=[for(source in sources) check(source)^];
{cases:len(results),supported:all([for(r in results) r.supported]),
 text:all([for(r in results) r.text]),diagnostics:all([for(r in results) r.diagnostics]),
 tree:all([for(r in results) r.tree])};
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "script-math-punctuation-main.tex";
let child="script-math-punctuation-child.tex";
let ex=profile.expand_with_kernel(input(path,"text")^,kernel,
 {source_name:path,echo_definitions:true,adapters:[],resources:{local:{[child]:{data:input(root ++ child,"text")^}},bundled:{}}},100000)^;
let result=digest.digest(ex)^; let built=reconstruct.reconstruct(ex)^;
let native=input(path,"latex")^; let tex=input(path,"tex")^;
{supported:result.issue==null,text:built.text==tex.text,
 diagnostics:ex.diagnostic_records==tex.diagnostics,tree:result.ast==native}
