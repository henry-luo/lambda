import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
fn check(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let n=parse(source,{type:"tex"})^;let b=reconstruct.reconstruct(r)^;
 {issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}
}
[for (source in [
  "\\ExplSyntaxOn\\meaning\\tl_put_right:Nn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\tl_gput_right:Nn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\seq_put_right:Nn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\seq_gput_right:Nn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_put_right:Nn\\l_a_tl{A}\\tl_use:N\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}{\\tl_put_right:Nn\\l_a_tl{B}\\tl_use:N\\l_a_tl}/\\tl_use:N\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}{\\tl_gput_right:Nn\\l_a_tl{B}}\\tl_use:N\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\globaldefs=1 {\\tl_put_right:Nn\\l_a_tl{A}}\\globaldefs=0 \\tl_use:N\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\globaldefs=-1 {\\tl_gput_right:Nn\\l_a_tl{A}}\\globaldefs=0 [\\tl_use:N\\l_a_tl]\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\afterassignment X\\tl_put_right:Nn\\l_a_tl{A}\\tl_use:N\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\edef\\p{\\tl_put_right:Nn\\noexpand\\l_a_tl{A}}\\p\\tl_use:N\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\def\\a{Q}\\tl_put_right:Nn\\l_a_tl{\\a}\\def\\a{Z}\\tl_use:N\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_put_right:Nn\\l_missing_tl{X}\\meaning\\l_missing_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\protected\\def\\mytl{A}\\tl_put_right:Nn\\mytl{B}\\meaning\\mytl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{a}\\seq_put_right:Nn\\l_s_seq{b}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\let\\stop\\relax\\catcode`x=12 \\seq_put_right:Nn\\l_s_seq{x}\\catcode120=11\\stop\\ifcat\\seq_item:Nn\\l_s_seq{1}a Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\let\\stop\\relax\\catcode`x=12 \\tl_put_right:Nn\\l_a_tl{x}\\catcode120=11\\stop\\ifcat\\tl_use:N\\l_a_tl a Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{}\\seq_put_right:Nn\\l_s_seq{{a,b}}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\let\\f\\seq_put_right:Nn\\f\\l_s_seq{a}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\protected\\long\\def\\f#1#2{\\tl_put_right:Nn#1{\\__seq_item:n{#2}}}\\ifx\\f\\seq_put_right:Nn Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\protected\\long\\def\\f#1#2{\\__kernel_tl_set:Nx#1{\\__kernel_exp_not:w\\exp_after:wN{#1#2}}}\\ifx\\f\\tl_put_right:Nn Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\cs_set_protected:Npn\\tl_put_right:Nn#1#2{OVERRIDE}\\seq_put_right:Nn\\l_s_seq{a}/\\seq_count:N\\l_s_seq\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\cs_set_protected:Npn\\tl_gput_right:Nn#1#2{OVERRIDE}\\seq_gput_right:Nn\\l_s_seq{a}/\\seq_count:N\\l_s_seq\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\let\\f\\tl_put_right:Nn\\f\\l_a_tl{A}\\tl_use:N\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{{a}{b}}\\seq_put_right:Nn\\l_s_seq{c}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\globaldefs=1 {\\seq_put_right:Nn\\l_s_seq{A}}\\globaldefs=0 \\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\globaldefs=-1 {\\seq_gput_right:Nn\\l_s_seq{A}}\\globaldefs=0 [\\seq_use:Nn\\l_s_seq{|}]\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\def\\a{Q}\\seq_put_right:Nn\\l_s_seq{\\a}\\def\\a{Z}\\seq_item:Nn\\l_s_seq{1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\afterassignment X\\tl_put_right:Nn\\l_a_tl{B}\\tl_use:N\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\tl_put_right:Nn\\l_a_tl{\\tl_use:N\\l_a_tl}\\tl_count:N\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{a}\\edef\\saved{\\seq_item:Nn\\l_s_seq{1}}\\saved\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\show\\seq_put_right:Nn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\show\\tl_put_right:Nn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{a}\\meaning\\l_s_seq\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_put_right:Nn\\l_a_tl{\u03b1\u85cf}\\tl_use:N\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{a}\\seq_if_empty:NTF\\l_s_seq{Y}{N}\\ExplSyntaxOff"
]) check(source)^]

fn safe_digestion(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 if(d.ast!=null) d.ast==(parse(source,{type:"latex"})^) else d.issue=="latex-digest-unsupported"
}
[for(source in ["\\ExplSyntaxOn\\tl_put_right:Nn\\l_tmpa_tl{A}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\begin{document}\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff\\end{document}"]) safe_digestion(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_seq_macro_main.tex","text")^;
let resources={local:{'script_seq_macro_child.tex':{data:input(root ++ "script_seq_macro_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_seq_macro_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;let n=input(root ++ "script_seq_macro_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}
