import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
fn check(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let n=parse(source,{type:"tex"})^;let b=reconstruct.reconstruct(r)^;
 {issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}
}
[for(source in [
  "\\ExplSyntaxOn\\meaning\\tl_set_eq:NN/\\meaning\\tl_gset_eq:NN/\\meaning\\seq_set_eq:NN/\\meaning\\seq_gset_eq:NN\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\show\\tl_set_eq:NN\\show\\tl_gset_eq:NN\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\tl_set_eq:NN\\seq_set_eq:NN Y\\else N\\fi/\\ifx\\tl_gset_eq:NN\\seq_gset_eq:NN Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_new:N\\l_b_tl\\tl_set:Nn\\l_a_tl{A}\\tl_set_eq:NN\\l_b_tl\\l_a_tl\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_new:N\\l_b_tl\\tl_set:Nn\\l_a_tl{A}\\tl_set_eq:NN\\l_b_tl\\l_a_tl\\tl_set:Nn\\l_a_tl{B}\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_new:N\\l_b_tl\\tl_set:Nn\\l_a_tl{A}{\\tl_set_eq:NN\\l_b_tl\\l_a_tl\\tl_use:N\\l_b_tl}[\\tl_use:N\\l_b_tl]\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_new:N\\l_b_tl\\tl_set:Nn\\l_a_tl{A}{\\tl_gset_eq:NN\\l_b_tl\\l_a_tl}\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_new:N\\l_b_tl\\tl_set:Nn\\l_a_tl{A}\\globaldefs=1 {\\tl_set_eq:NN\\l_b_tl\\l_a_tl}\\globaldefs=0 \\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_new:N\\l_b_tl\\tl_set:Nn\\l_a_tl{A}\\globaldefs=-1 {\\tl_gset_eq:NN\\l_b_tl\\l_a_tl}\\globaldefs=0 [\\tl_use:N\\l_b_tl]\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_new:N\\l_b_tl\\tl_set:Nn\\l_a_tl{A}\\afterassignment X\\tl_set_eq:NN\\l_b_tl\\l_a_tl\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_new:N\\l_b_tl\\tl_set:Nn\\l_a_tl{A}\\afterassignment X\\tl_gset_eq:NN\\l_b_tl\\l_a_tl\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_new:N\\l_b_tl\\tl_set:Nn\\l_a_tl{A}\\let\\f\\tl_set_eq:NN\\f\\l_b_tl\\l_a_tl\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_new:N\\l_b_tl\\tl_set:Nn\\l_a_tl{A}\\edef\\f{\\tl_set_eq:NN\\noexpand\\l_b_tl\\noexpand\\l_a_tl}\\f\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_set_eq:NN\\l_missing_tl\\other_missing\\meaning\\l_missing_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_set_eq:NN\\l_a_tl\\scan_stop:\\meaning\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_a_seq\\seq_new:N\\l_b_seq\\seq_put_right:Nn\\l_a_seq{a}\\seq_set_eq:NN\\l_b_seq\\l_a_seq\\seq_use:Nn\\l_b_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_a_seq\\seq_new:N\\l_b_seq\\seq_put_right:Nn\\l_a_seq{a}{\\seq_gset_eq:NN\\l_b_seq\\l_a_seq}\\seq_use:Nn\\l_b_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_a_seq\\seq_new:N\\l_b_seq\\seq_put_right:Nn\\l_a_seq{a}\\seq_set_eq:NN\\l_b_seq\\l_a_seq\\seq_put_right:Nn\\l_a_seq{b}\\seq_use:Nn\\l_b_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{藏α}\\tl_set_eq:NN\\l_b_tl\\l_a_tl\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\let\\z\\tex_let:D\\z\\l_b_tl=\\l_a_tl\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\scan_stop:/\\ifx\\scan_stop:\\relax Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\tex_let:D\\let Y\\else N\\fi/\\ifx\\tex_global:D\\global Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\let\\copy\\seq_set_eq:NN\\tl_set_eq:NN\\l_b_tl\\l_a_tl\\cs_set_protected:Npn\\tl_set_eq:NN#1#2{OVERRIDE}\\copy\\l_c_tl\\l_a_tl\\tl_use:N\\l_b_tl/\\tl_use:N\\l_c_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\let\\q\\tex_let:D\\global\\q\\l_b_tl=\\l_a_tl\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\seq_set_eq:NN\\l_a_tl\\l_a_tl\\tl_use:N\\l_a_tl\\ExplSyntaxOff"
]) check(source)^]
fn guarded(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 d.ast==null and d.issue=="latex-digest-unsupported" and d.unsupported=="expl3 token-list assignment source mapping"
}
[for(source in [
 "\\ExplSyntaxOn\\tl_set_eq:NN\\l_tmpa_tl\\l_tmpb_tl\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\tl_gset_eq:NN\\l_tmpa_tl\\l_tmpb_tl\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\let\\q\\tex_let:D\\global\\q\\l_tmpa_tl=\\l_tmpb_tl\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\let\\q\\tl_set_eq:NN\\q\\l_tmpa_tl\\l_tmpb_tl\\ExplSyntaxOff\\begin{document}Hello\\end{document}"
]) guarded(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_tl_set_eq_main.tex","text")^;
let resources={local:{'script_tl_set_eq_child.tex':{data:input(root ++ "script_tl_set_eq_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_tl_set_eq_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;let n=input(root ++ "script_tl_set_eq_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}
