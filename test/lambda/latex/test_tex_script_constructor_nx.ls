import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true,constructor_argument_mode:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
fn check(source) any^ {
 let ex=profile.expand_with_kernel(source,kernel,options,100000)^;
 let d=digest.digest(ex)^; let tex=parse(source,{type:"tex"})^; let native=parse(source,{type:"latex"})^;
 {supported:d.issue==null,text:reconstruct.reconstruct(ex)^.text==tex.text,
  messages:ex.messages==tex.messages,diagnostics:ex.diagnostic_records==tex.diagnostics,tree:d.ast==native}
}
let variants=["tl_set:Nx","tl_gset:Nx","tl_put_left:Nx","tl_put_right:Nx","tl_gput_left:Nx","tl_gput_right:Nx"];
let base=[for(name in variants,wrap in ["textbf","unknown"])
 "\\ExplSyntaxOn\\begin{document}\\" ++ wrap ++ "{\\" ++ name ++ "\\l_tmpa_tl{A}B}\\end{document}"];
let extra=[
 "\\ExplSyntaxOn\\def\\q{Z}\\begin{document}\\textbf{\\tl_set:Nx\\l_tmpa_tl{\\q}B}\\end{document}",
 "\\ExplSyntaxOn\\def\\q{Z}\\begin{document}\\textbf{\\tl_set:Nx\\l_tmpa_tl{\\noexpand\\q}B}\\end{document}",
 "\\ExplSyntaxOn\\def\\q{Z}\\begin{document}\\textbf{\\tl_set:Nx\\l_tmpa_tl{\\unexpanded{\\q}}B}\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textbf{\\tl_set:Nx\\l_tmpa_tl{{A}{B}}C}\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textbf{\\tl_set:Nx\\l_tmpa_tl{}B}\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textbf{\\tl_set:Nx\\l_tmpa_tl{\\textit{A}}B}\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textbf{\\tl_set:Nx\\l_tmpa_tl{A}\\tl_set:Nx\\l_tmpa_tl{B}}\\end{document}",
 "\\ExplSyntaxOn\\let\\foo\\tl_set:Nx\\begin{document}\\textbf{\\foo\\l_tmpa_tl{A}B}\\end{document}",
 "\\ExplSyntaxOn\\cs_set_protected:Npn\\tl_set:Nx#1#2{RED}\\begin{document}\\textbf{\\tl_set:Nx\\l_tmpa_tl{A}B}\\end{document}",
 "\\ExplSyntaxOn\\tl_set:Nn\\l__exp_internal_tl{X}\\begin{document}\\textbf{\\tl_set:Nx\\l_tmpa_tl{A}B}\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textbf{\\tl_put_right:Nx\\l_tmpa_tl{\\tl_use:N\\l_tmpb_tl}B}\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\section{\\tl_set:Nx\\l_tmpa_tl{A}B}\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textbf{A\\tl_set:Nx\\l_tmpa_tl{B}C}\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textbf{\\exp_args:NNx\\tl_set:Nn\\l_tmpa_tl{A}B}\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textbf{\\use:x{AB}C}\\end{document}",
 "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nx\\l_a_tl{ABC}\\tl_use:N\\l_a_tl\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\use:x{ABC}\\ExplSyntaxOff"
];
let results=[for(source in [*base,*extra]) check(source)^];
let shows=[for(name in ["use:x","exp_args:NNx","cs_set_nopar:Npx","l__exp_internal_tl"]) {
 let source="\\ExplSyntaxOn\\show\\" ++ name ++ "\\ExplSyntaxOff";
 let ex=profile.expand_with_kernel(source,kernel,options,100000)^; let tex=parse(source,{type:"tex"})^;
 ex.issue==null and reconstruct.reconstruct(ex)^.text==tex.text and ex.messages==tex.messages and ex.diagnostic_records==tex.diagnostics
}];
{cases:len(results),supported:all([for(r in results) r.supported]),text:all([for(r in results) r.text]),
 messages:all([for(r in results) r.messages]),diagnostics:all([for(r in results) r.diagnostics]),
 tree:all([for(r in results) r.tree]),shows:all(shows)};
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "script-constructor-nx-main.tex";
let child="script-constructor-nx-child.tex";
let ex=profile.expand_with_kernel(input(path,"text")^,kernel,
 {source_name:path,script_expl3:true,constructor_argument_mode:true,echo_definitions:true,adapters:[],
  resources:{local:{[child]:{data:input(root ++ child,"text")^}},bundled:{}}},100000)^;
let d=digest.digest(ex)^; let native=input(path,"latex")^; let tex=input(path,"tex")^;
{supported:d.issue==null,text:reconstruct.reconstruct(ex)^.text==tex.text,
 messages:ex.messages==tex.messages,diagnostics:ex.diagnostic_records==tex.diagnostics,tree:d.ast==native}
