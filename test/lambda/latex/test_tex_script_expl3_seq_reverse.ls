import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let baseline=parse("",{type:"tex"})^.messages;
fn check(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let n=parse(source,{type:"tex"})^;let b=reconstruct.reconstruct(r)^;
 {issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==slice(n.messages,len(baseline),len(n.messages))^,diagnostics_match:r.diagnostic_records==n.diagnostics}
}
[for (source in [
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_reverse:N\\l_s_seq[\\seq_use:Nn\\l_s_seq{|}]\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_reverse:N\\l_s_seq\\seq_use:Nn\\l_s_seq{,}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a}\\seq_reverse:N\\l_s_seq\\seq_use:Nn\\l_s_seq{,}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_reverse:N\\l_s_seq\\seq_reverse:N\\l_s_seq\\seq_use:Nn\\l_s_seq{,}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}{\\seq_reverse:N\\l_s_seq\\seq_use:Nn\\l_s_seq{,}}/\\seq_use:Nn\\l_s_seq{,}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}{\\seq_greverse:N\\l_s_seq}\\seq_use:Nn\\l_s_seq{,}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}{\\globaldefs=1\\seq_reverse:N\\l_s_seq}\\seq_use:Nn\\l_s_seq{,}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}{\\globaldefs=-1\\seq_greverse:N\\l_s_seq}\\seq_use:Nn\\l_s_seq{,}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\let\\x\\l_s_seq\\seq_reverse:N\\l_s_seq\\seq_use:Nn\\x{|}/\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,{b,c},d}\\seq_reverse:N\\l_s_seq\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{}\\seq_put_right:Nn\\l_s_seq{A}\\seq_reverse:N\\l_s_seq[\\seq_use:Nn\\l_s_seq{|}]\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\def\\word{A}\\seq_put_right:Nn\\l_s_seq{\\word}\\seq_put_right:Nn\\l_s_seq{B}\\seq_reverse:N\\l_s_seq\\def\\word{Z}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\let\\rev\\seq_reverse:N\\rev\\l_s_seq\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_reverse:N\\l_s_seq\\seq_count:N\\l_s_seq\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b}\\seq_reverse:N\\l_s_seq\\meaning\\l_s_seq\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_reverse:N\\l_s_seq\\seq_if_empty:NTF\\l_s_seq{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b}\\seq_reverse:N\\l_s_seq\\seq_if_empty:NTF\\l_s_seq{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\edef\\rev{\\seq_reverse:N\\noexpand\\l_s_seq}\\rev\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_greverse:N\\l_s_seq\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_left:Nn\\l_s_seq{\u03b1}\\seq_put_right:Nn\\l_s_seq{\u03b2}\\seq_reverse:N\\l_s_seq\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{{a}{b}}\\seq_put_right:Nn\\l_s_seq{c}\\seq_reverse:N\\l_s_seq\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_reverse:N\\l_s_seq\\seq_put_left:Nn\\l_s_seq{X}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_reverse:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{X}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_greverse:N\\l_s_seq\\seq_reverse:N\\l_s_seq\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}{\\seq_reverse:N\\l_s_seq\\seq_greverse:N\\l_s_seq}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b}\\seq_reverse:N\\l_s_seq\\seq_clear:N\\l_s_seq\\seq_if_empty:NTF\\l_s_seq{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_reverse:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{a}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\def\\r#1{\\seq_reverse:N#1}\\r\\l_s_seq\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\iftrue\\seq_reverse:N\\l_s_seq\\else X\\fi\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\iffalse\\seq_reverse:N\\l_s_seq\\fi\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_greverse:N\\l_s_seq\\seq_count:N\\l_s_seq\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{~}\\seq_put_right:Nn\\l_s_seq{A}\\seq_reverse:N\\l_s_seq[\\seq_use:Nn\\l_s_seq{|}]\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_reverse:N\\l_s_seq\\seq_use:Nn\\l_s_seq{~}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\let\\x\\l_s_seq\\seq_greverse:N\\l_s_seq\\seq_use:Nn\\x{|}/\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff"
]) check(source)^]

fn safe_digestion(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 if(d.ast!=null) d.ast==(parse(source,{type:"latex"})^) else d.issue=="latex-digest-unsupported"
}
[for(source in ["\\begin{document}\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b}\\seq_reverse:N\\l_s_seq\\seq_use:Nn\\l_s_seq{,}\\ExplSyntaxOff\\end{document}","\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b}\\seq_reverse:N\\l_s_seq\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\begin{document}X\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_reverse:N\\l_s_seq\\ExplSyntaxOff Y\\end{document}"]) safe_digestion(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_seq_reverse_main.tex","text")^;
let resources={local:{'script_seq_reverse_child.tex':{data:input(root ++ "script_seq_reverse_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_seq_reverse_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;let n=input(root ++ "script_seq_reverse_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==slice(n.messages,len(baseline),len(n.messages))^,diagnostics_match:r.diagnostic_records==n.diagnostics}
