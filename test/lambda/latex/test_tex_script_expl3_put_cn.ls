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
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\tl_set:Nn\\l_s_tl{A}\\tl_put_left:cn{l_s_tl}{B}\\tl_use:N\\l_s_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\tl_put_left:cn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\tl_set:Nn\\l_s_tl{A}{\\tl_put_left:cn{l_s_tl}{B}}\\tl_use:N\\l_s_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\tl_set:Nn\\l_s_tl{A}\\tl_put_right:cn{l_s_tl}{B}\\tl_use:N\\l_s_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\tl_put_right:cn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\tl_set:Nn\\l_s_tl{A}{\\tl_put_right:cn{l_s_tl}{B}}\\tl_use:N\\l_s_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\tl_set:Nn\\l_s_tl{A}\\tl_gput_left:cn{l_s_tl}{B}\\tl_use:N\\l_s_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\tl_gput_left:cn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\tl_set:Nn\\l_s_tl{A}{\\tl_gput_left:cn{l_s_tl}{B}}\\tl_use:N\\l_s_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\tl_set:Nn\\l_s_tl{A}\\tl_gput_right:cn{l_s_tl}{B}\\tl_use:N\\l_s_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\tl_gput_right:cn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\tl_set:Nn\\l_s_tl{A}{\\tl_gput_right:cn{l_s_tl}{B}}\\tl_use:N\\l_s_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\seq_put_left:cn{l_s_seq}{B}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\seq_put_left:cn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}{\\seq_put_left:cn{l_s_seq}{B}}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\seq_put_right:cn{l_s_seq}{B}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\seq_put_right:cn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}{\\seq_put_right:cn{l_s_seq}{B}}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\seq_gput_left:cn{l_s_seq}{B}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\seq_gput_left:cn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}{\\seq_gput_left:cn{l_s_seq}{B}}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\seq_gput_right:cn{l_s_seq}{B}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\seq_gput_right:cn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}{\\seq_gput_right:cn{l_s_seq}{B}}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\def\\prefix{l_s}\\tl_put_left:cn{\\prefix _tl}{藏α}\\tl_use:N\\l_s_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\let\\q\\seq_put_left:cn\\q{l_s_seq}{A}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\afterassignment X\\seq_put_right:cn{l_s_seq}{A}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\edef\\q{\\tl_put_left:cn{l_s_tl}{A}}\\q\\tl_use:N\\l_s_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\show\\tl_put_left:cn\\show\\seq_gput_right:cn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_put_left:cn{missing}{A}\\expandafter\\meaning\\csname missing\\endcsname\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\globaldefs=1 {\\tl_put_left:cn{l_s_tl}{A}}\\globaldefs=0 \\tl_use:N\\l_s_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\globaldefs=-1 {\\seq_gput_right:cn{l_s_seq}{A}}\\globaldefs=0 [\\seq_use:Nn\\l_s_seq{|}]\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_left:cn{l_s_seq}{{a}{b}}\\seq_put_right:cn{l_s_seq}{}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\def\\a{A}\\seq_put_right:cn{l_s_seq}{\\a}\\def\\a{B}\\seq_item:Nn\\l_s_seq{1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\let\\stop\\relax\\catcode`x=12 \\tl_put_left:cn{l_s_tl}{x}\\catcode120=11\\stop\\ifcat\\tl_use:N\\l_s_tl a Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\let\\stop\\relax\\catcode`x=12 \\seq_put_left:cn{l_s_seq}{x}\\catcode120=11\\stop\\ifcat\\seq_item:Nn\\l_s_seq{1}a Y\\else N\\fi\\ExplSyntaxOff"
]) check(source)^]

fn guarded(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 d.ast==null and d.issue=="latex-digest-unsupported" and d.unsupported=="expl3 token-list assignment source mapping"
}
[for(source in [
 "\\ExplSyntaxOn\\tl_put_left:cn{l_tmpa_tl}{A}\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\tl_gput_right:cn{l_tmpa_tl}{A}\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_left:cn{l_s_seq}{A}\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\let\\q\\seq_gput_right:cn\\q{l_s_seq}{A}\\ExplSyntaxOff\\begin{document}Hello\\end{document}"
]) guarded(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_put_cn_main.tex","text")^;
let resources={local:{'script_put_cn_child.tex':{data:input(root ++ "script_put_cn_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_put_cn_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;let n=input(root ++ "script_put_cn_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}
