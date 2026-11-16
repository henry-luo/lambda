import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={constructor_argument_mode:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
fn check(source) any^ {
 let ex=profile.expand_with_kernel(source,kernel,options,100000)^;
 let d=digest.digest(ex)^; let tex=parse(source,{type:"tex"})^; let native=parse(source,{type:"latex"})^;
 {supported:d.issue==null,text:reconstruct.reconstruct(ex)^.text==tex.text,
  diagnostics:ex.diagnostic_records==tex.diagnostics,tree:d.ast==native}
}
let names=["alpha_beta","alpha:beta","alpha3beta","textbf_tail","emph_tail","u_foo","item_test",
 "quad_foo","unknown_name","_foo","@abc_foo","A.foo","A-foo","A@foo","z0"];
let sources=[for(name in names,wrap in ["textbf","unknown"])
 "\\begin{document}\\" ++ wrap ++ "{\\csname " ++ name ++ "\\endcsname{A}B}\\end{document}"];
let results=[for(source in sources) check(source)^];
{cases:len(results),supported:all([for(r in results) r.supported]),text:all([for(r in results) r.text]),
 diagnostics:all([for(r in results) r.diagnostics]),tree:all([for(r in results) r.tree])}
