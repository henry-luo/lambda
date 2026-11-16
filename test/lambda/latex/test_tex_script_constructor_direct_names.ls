import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true,constructor_argument_mode:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
fn check(source) any^ {
 let ex=profile.expand_with_kernel(source,kernel,options,100000)^;
 let d=digest.digest(ex)^; let tex=parse(source,{type:"tex"})^; let native=parse(source,{type:"latex"})^;
 {supported:d.issue==null,text:reconstruct.reconstruct(ex)^.text==tex.text,
  diagnostics:ex.diagnostic_records==tex.diagnostics,tree:d.ast==native}
}
let names=["xyz_abc:Nn","foo:bar","u_foo","textbf_tail","__xyz","@abc_foo"];
let tails=["{A}B"," X","[Q]{A}"];
let sources=[for(name in names,tail in tails,wrap in ["textbf","unknown"])
 "\\ExplSyntaxOn\\begin{document}\\" ++ wrap ++ "{\\" ++ name ++ tail ++ "}\\end{document}"];
let results=[for(source in sources) check(source)^];
let guards=["\\ExplSyntaxOn\\begin{document}\\textbf{\\éfoo{X}}\\end{document}",
 "\\begin{document}\\textbf{\\csname éfoo\\endcsname{X}}\\end{document}"];
fn guarded(source) any^ {
 let ex=profile.expand_with_kernel(source,kernel,options,100000)^; let d=digest.digest(ex)^;
 d.issue=="latex-digest-unsupported" and d.unsupported=="native command unicode source mapping"
}
{cases:len(results),supported:all([for(r in results) r.supported]),text:all([for(r in results) r.text]),
 diagnostics:all([for(r in results) r.diagnostics]),tree:all([for(r in results) r.tree]),guards:all([for(s in guards) guarded(s)^])}
