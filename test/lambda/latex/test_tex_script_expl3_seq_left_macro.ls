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
  "\\ExplSyntaxOn\\meaning\\seq_put_left:Nn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\seq_gput_left:Nn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_left:Nn\\l_s_seq{a}\\seq_put_left:Nn\\l_s_seq{b}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\let\\stop\\relax\\catcode`x=12 \\seq_put_left:Nn\\l_s_seq{x}\\catcode120=11\\stop\\ifcat\\seq_item:Nn\\l_s_seq{1}a Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_left:Nn\\l_s_seq{}\\seq_put_left:Nn\\l_s_seq{{a,b}}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\let\\f\\seq_put_left:Nn\\f\\l_s_seq{a}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\protected\\long\\def\\f#1#2{\\tl_put_right:Nn#1{\\__seq_item:n{#2}}}\\ifx\\f\\seq_put_left:Nn Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\cs_set_protected:Npn\\tl_put_right:Nn#1#2{OVERRIDE}\\seq_put_left:Nn\\l_s_seq{a}/\\seq_count:N\\l_s_seq\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\cs_set_protected:Npn\\tl_gput_right:Nn#1#2{OVERRIDE}\\seq_gput_left:Nn\\l_s_seq{a}/\\seq_count:N\\l_s_seq\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_left:Nn\\l_s_seq{{a}{b}}\\seq_put_left:Nn\\l_s_seq{c}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\globaldefs=1 {\\seq_put_left:Nn\\l_s_seq{A}}\\globaldefs=0 \\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\globaldefs=-1 {\\seq_gput_left:Nn\\l_s_seq{A}}\\globaldefs=0 [\\seq_use:Nn\\l_s_seq{|}]\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\def\\a{Q}\\seq_put_left:Nn\\l_s_seq{\\a}\\def\\a{Z}\\seq_item:Nn\\l_s_seq{1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_left:Nn\\l_s_seq{a}\\edef\\saved{\\seq_item:Nn\\l_s_seq{1}}\\saved\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\show\\seq_put_left:Nn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_left:Nn\\l_s_seq{a}\\meaning\\l_s_seq\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_left:Nn\\l_s_seq{a}\\seq_if_empty:NTF\\l_s_seq{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\seq_gput_left:Nn/\\meaning\\exp_not:f/\\meaning\\exp_end_continue_f:w/\\meaning\\exp_stop_f:\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\show\\seq_put_left:Nn\\show\\seq_gput_left:Nn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{a}\\seq_put_left:Nn\\l_s_seq{b}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n a\\__seq_item:n b}\\seq_put_left:Nn\\s{c}\\meaning\\s/\\seq_use:Nn\\s{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{a}\\seq_put_left:Nn\\l_s_seq{b}\\seq_if_in:NnTF\\l_s_seq{b}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\def\\a{A}\\seq_put_left:Nn\\l_s_seq{\\a}\\def\\a{B}\\seq_item:Nn\\l_s_seq{1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_left:Nn\\l_s_seq{藏α}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_left:Nn\\l_s_seq{{a}{b}}\\seq_put_left:Nn\\l_s_seq{}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\afterassignment X\\seq_put_left:Nn\\l_s_seq{a}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\let\\stop\\relax\\catcode`x=12 \\seq_put_left:Nn\\l_s_seq{x}\\catcode120=11\\stop\\ifcat\\seq_item:Nn\\l_s_seq{1}a Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\romannumeral`A\\exp_stop_f:!\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\number123\\exp_stop_f:!\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\number\\exp_stop_f:123!\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\edef\\z{\\exp_not:f{\\exp_after:wN\\use:n\\exp_after:wN{AB}}}\\meaning\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\edef\\z{\\exp_not:f{\\foo}}\\meaning\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{\\bar}\\def\\bar{AB}\\edef\\z{\\exp_not:f{\\foo}}\\meaning\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\protected\\def\\foo{A}\\edef\\z{\\exp_not:f{\\foo}}\\meaning\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{AB}\\edef\\z{\\exp_not:f{{\\foo}}}\\meaning\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{AB}\\edef\\z{\\exp_not:f{\\scan_stop:\\foo}}\\meaning\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\edef\\z{\\exp_not:f{~abc}}\\meaning\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\sp\\exp_stop_f:\\number\\sp 123!\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\sp\\exp_stop_f:\\number123\\sp!\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\sp\\exp_stop_f:\\romannumeral`A\\sp!\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\sp\\exp_stop_f:\\int_eval:n{\\sp 1+2}!\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\sp\\exp_stop_f:\\ifnum\\sp 1=1 YES\\else NO\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\protected\\def\\s{\\s__seq\\__seq_item:n{a}}\\seq_put_left:Nn\\s{b}\\meaning\\s\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\long\\def\\s{\\s__seq\\__seq_item:n a}\\seq_put_left:Nn\\s{b}\\meaning\\s\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq}\\seq_put_left:Nn\\s{b}\\meaning\\s\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\globaldefs=-1 {\\seq_gput_left:Nn\\l_s_seq{a}}\\globaldefs=0 [\\seq_use:Nn\\l_s_seq{|}]\\ExplSyntaxOff"
]) check(source)^]

fn safe_digestion(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 if(d.ast!=null) d.ast==(parse(source,{type:"latex"})^) else d.issue=="latex-digest-unsupported"
}
[for(source in ["\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_left:Nn\\l_s_seq{A}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\begin{document}\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_left:Nn\\l_s_seq{A}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff\\end{document}"]) safe_digestion(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_seq_left_main.tex","text")^;
let resources={local:{'script_seq_left_child.tex':{data:input(root ++ "script_seq_left_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_seq_left_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;let n=input(root ++ "script_seq_left_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}
