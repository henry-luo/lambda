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
  "\\ExplSyntaxOn\\meaning\\tl_put_left:Nn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\tl_gput_left:Nn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_put_left:Nn\\l_a_tl{A}\\tl_use:N\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}{\\tl_put_left:Nn\\l_a_tl{B}\\tl_use:N\\l_a_tl}/\\tl_use:N\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}{\\tl_gput_left:Nn\\l_a_tl{B}}\\tl_use:N\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\globaldefs=1 {\\tl_put_left:Nn\\l_a_tl{A}}\\globaldefs=0 \\tl_use:N\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\globaldefs=-1 {\\tl_gput_left:Nn\\l_a_tl{A}}\\globaldefs=0 [\\tl_use:N\\l_a_tl]\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\afterassignment X\\tl_put_left:Nn\\l_a_tl{A}\\tl_use:N\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\edef\\p{\\tl_put_left:Nn\\noexpand\\l_a_tl{A}}\\p\\tl_use:N\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\def\\a{Q}\\tl_put_left:Nn\\l_a_tl{\\a}\\def\\a{Z}\\tl_use:N\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_put_left:Nn\\l_missing_tl{X}\\meaning\\l_missing_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\protected\\def\\mytl{A}\\tl_put_left:Nn\\mytl{B}\\meaning\\mytl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\let\\stop\\relax\\catcode`x=12 \\tl_put_left:Nn\\l_a_tl{x}\\catcode120=11\\stop\\ifcat\\tl_use:N\\l_a_tl a Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\protected\\long\\def\\f#1#2{\\__kernel_tl_set:Nx#1{\\__kernel_exp_not:w\\exp_after:wN{#1#2}}}\\ifx\\f\\tl_put_left:Nn Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\let\\f\\tl_put_left:Nn\\f\\l_a_tl{A}\\tl_use:N\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\afterassignment X\\tl_put_left:Nn\\l_a_tl{B}\\tl_use:N\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\tl_put_left:Nn\\l_a_tl{\\tl_use:N\\l_a_tl}\\tl_count:N\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\show\\tl_put_left:Nn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_put_left:Nn\\l_a_tl{\u03b1\u85cf}\\tl_use:N\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\tl_gput_left:Nn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\show\\tl_gput_left:Nn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\protected\\long\\def\\f#1#2{\\__kernel_tl_set:Nx#1{\\__kernel_exp_not:w{#2}\\__kernel_exp_not:w\\exp_after:wN{#1}}}\\ifx\\f\\tl_put_left:Nn Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\tl_put_left:Nn\\l_a_tl{}\\tl_use:N\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{{A}{B}}\\tl_put_left:Nn\\l_a_tl{{C}{D}}\\meaning\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\let\\stop\\relax\\catcode`x=12 \\tl_put_left:Nn\\l_a_tl{x}\\catcode120=11\\stop\\ifcat\\tl_use:N\\l_a_tl a Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_put_left:Nn\\l_a_tl{α藏}\\tl_use:N\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\afterassignment X\\tl_gput_left:Nn\\l_a_tl{B}\\tl_use:N\\l_a_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\let\\g\\tl_gput_left:Nn{\\g\\l_a_tl{B}}\\tl_use:N\\l_a_tl\\ExplSyntaxOff"
]) check(source)^]

fn safe_digestion(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 if(d.ast!=null) d.ast==(parse(source,{type:"latex"})^) else d.issue=="latex-digest-unsupported"
}
[for(source in ["\\ExplSyntaxOn\\tl_put_left:Nn\\l_tmpa_tl{A}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\begin{document}\\ExplSyntaxOn\\tl_put_left:Nn\\l_tmpa_tl{A}\\tl_use:N\\l_tmpa_tl\\ExplSyntaxOff\\end{document}"]) safe_digestion(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_tl_left_main.tex","text")^;
let resources={local:{'script_tl_left_child.tex':{data:input(root ++ "script_tl_left_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_tl_left_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;let n=input(root ++ "script_tl_left_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}
