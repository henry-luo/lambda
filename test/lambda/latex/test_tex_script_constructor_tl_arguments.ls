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
let sources=[
 "\\ExplSyntaxOn\\begin{document}\\textbf{\\tl_put_right:Nn\\l_tmpa_tl{A}B}\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textit{\\tl_gput_right:Nn\\g_tmpa_tl{Q}R}\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\emph{\\tl_set:Nn\\l_tmpa_tl{A}B}\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textbf{\\tl_gset:Nn\\g_tmpa_tl{A}B}\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textbf{A\\tl_put_right:Nn\\l_tmpa_tl{B}C}\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textbf{\\tl_put_right:Nn\\l_tmpa_tl{A{B}}C}\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textbf{\\tl_put_right:Nn\\l_tmpa_tl{}C}\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textbf{\\tl_put_right:Nn\\l_tmpa_tl{\\textit{X}}Y}\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textbf{\\tl_put_right:Nn\\l_tmpa_tl{A}\\tl_put_right:Nn\\l_tmpa_tl{B}}\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\section{\\tl_put_right:Nn\\l_tmpa_tl{A}B}\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\unknown{\\tl_put_right:Nn\\l_tmpa_tl{A}B}\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textbf{\\tl_put_left:Nn\\l_tmpa_tl{A}B}\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textbf{\\tl_set:Nx\\l_tmpa_tl{A}B}\\end{document}"
];
let results=[for(source in sources) check(source)^];
let guards=["\\ExplSyntaxOn\\begin{document}\\textbf{\\seq_put_right:Nn\\l_tmpa_seq{A}B}\\end{document}"];
fn guarded(source) any^ {
 let ex=profile.expand_with_kernel(source,kernel,options,100000)^;
 ex.issue=="tex-unsupported-primitive" and ex.unsupported=="constructor argument expl3 assignment"
}
{cases:len(results),supported:all([for(r in results) r.supported]),text:all([for(r in results) r.text]),
 diagnostics:all([for(r in results) r.diagnostics]),tree:all([for(r in results) r.tree]),guards:all([for(s in guards) guarded(s)^])};
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "script-constructor-tl-main.tex";
let child="script-constructor-tl-child.tex";
let ex=profile.expand_with_kernel(input(path,"text")^,kernel,
 {source_name:path,script_expl3:true,constructor_argument_mode:true,echo_definitions:true,adapters:[],
  resources:{local:{[child]:{data:input(root ++ child,"text")^}},bundled:{}}},100000)^;
let d=digest.digest(ex)^; let native=input(path,"latex")^; let tex=input(path,"tex")^;
{supported:d.issue==null,text:reconstruct.reconstruct(ex)^.text==tex.text,
 diagnostics:ex.diagnostic_records==tex.diagnostics,tree:d.ast==native}
