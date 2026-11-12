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
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\tl_set_eq:cN{l_b_tl}\\l_a_tl\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\tl_set_eq:cN\\tl_set_eq:cN Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\tl_set_eq:Nc\\l_b_tl{l_a_tl}\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\tl_set_eq:Nc\\tl_set_eq:Nc Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\tl_set_eq:cc{l_b_tl}{l_a_tl}\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\tl_set_eq:cc\\tl_set_eq:cc Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\tl_gset_eq:cN{l_b_tl}\\l_a_tl\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\tl_gset_eq:cN\\tl_gset_eq:cN Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\tl_gset_eq:Nc\\l_b_tl{l_a_tl}\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\tl_gset_eq:Nc\\tl_gset_eq:Nc Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\tl_gset_eq:cc{l_b_tl}{l_a_tl}\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\tl_gset_eq:cc\\tl_gset_eq:cc Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_a_seq\\seq_put_right:Nn\\l_a_seq{A}\\seq_set_eq:cN{l_b_seq}\\l_a_seq\\seq_use:Nn\\l_b_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\seq_set_eq:cN\\tl_set_eq:cN Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_a_seq\\seq_put_right:Nn\\l_a_seq{A}\\seq_set_eq:Nc\\l_b_seq{l_a_seq}\\seq_use:Nn\\l_b_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\seq_set_eq:Nc\\tl_set_eq:Nc Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_a_seq\\seq_put_right:Nn\\l_a_seq{A}\\seq_set_eq:cc{l_b_seq}{l_a_seq}\\seq_use:Nn\\l_b_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\seq_set_eq:cc\\tl_set_eq:cc Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_a_seq\\seq_put_right:Nn\\l_a_seq{A}\\seq_gset_eq:cN{l_b_seq}\\l_a_seq\\seq_use:Nn\\l_b_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\seq_gset_eq:cN\\tl_gset_eq:cN Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_a_seq\\seq_put_right:Nn\\l_a_seq{A}\\seq_gset_eq:Nc\\l_b_seq{l_a_seq}\\seq_use:Nn\\l_b_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\seq_gset_eq:Nc\\tl_gset_eq:Nc Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_a_seq\\seq_put_right:Nn\\l_a_seq{A}\\seq_gset_eq:cc{l_b_seq}{l_a_seq}\\seq_use:Nn\\l_b_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\seq_gset_eq:cc\\tl_gset_eq:cc Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\tl_set_eq:cN/\\meaning\\tl_set_eq:Nc/\\meaning\\tl_set_eq:cc\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\seq_gset_eq:cN/\\meaning\\seq_gset_eq:Nc/\\meaning\\seq_gset_eq:cc\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\show\\seq_set_eq:cN\\show\\tl_gset_eq:cc\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}{\\tl_gset_eq:cN{l_b_tl}\\l_a_tl}\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\globaldefs=-1 {\\tl_gset_eq:cN{l_b_tl}\\l_a_tl}\\globaldefs=0 \\meaning\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\afterassignment X\\tl_set_eq:Nc\\l_b_tl{l_a_tl}\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_set_eq:Nc\\l_b_tl{missing}\\meaning\\l_b_tl/\\ifcsname missing\\endcsname Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_a_seq\\seq_put_right:Nn\\l_a_seq{A}\\let\\q\\seq_set_eq:cc\\q{l_b_seq}{l_a_seq}\\seq_use:Nn\\l_b_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\def\\prefix{l_a}\\tl_set_eq:Nc\\l_b_tl{\\prefix _tl}\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_a_seq\\seq_put_right:Nn\\l_a_seq{藏α}\\seq_set_eq:cc{l_b_seq}{l_a_seq}\\seq_use:Nn\\l_b_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\globaldefs=1 {\\tl_set_eq:cc{l_b_tl}{l_a_tl}}\\globaldefs=0 \\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\let\\q\\seq_set_eq:cc\\cs_set_protected:Npn\\tl_set_eq:cc#1#2{OVERRIDE}\\q{l_b_tl}{l_a_tl}\\tl_use:N\\l_b_tl\\ExplSyntaxOff"
]) check(source)^]

fn guarded(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 d.ast==null and d.issue=="latex-digest-unsupported" and d.unsupported=="expl3 token-list assignment source mapping"
}
[for(source in [
 "\\ExplSyntaxOn\\tl_set_eq:cN{l_tmpa_tl}\\l_tmpb_tl\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\tl_gset_eq:Nc\\l_tmpa_tl{l_tmpb_tl}\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\seq_set_eq:cc{l_tmpa_tl}{l_tmpb_tl}\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\let\\q\\seq_gset_eq:cc\\q{l_tmpa_tl}{l_tmpb_tl}\\ExplSyntaxOff\\begin{document}Hello\\end{document}"
]) guarded(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_tl_set_eq_c_main.tex","text")^;
let resources={local:{'script_tl_set_eq_c_child.tex':{data:input(root ++ "script_tl_set_eq_c_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_tl_set_eq_c_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;let n=input(root ++ "script_tl_set_eq_c_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}
