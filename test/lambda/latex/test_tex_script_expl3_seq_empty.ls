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
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_if_empty:NTF\\l_s_seq{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_if_empty:NT\\l_s_seq{Y}X\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_if_empty:NF\\l_s_seq{N}X\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\seq_if_empty:NTF\\l_s_seq{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\seq_if_empty:NT\\l_s_seq{Y}X\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\seq_if_empty:NF\\l_s_seq{N}X\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_if_empty:NTF\\undefined{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{}\\seq_if_empty:NTF\\foo{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{\\s__seq}\\seq_if_empty:NTF\\foo{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\long\\def\\foo{\\s__seq}\\seq_if_empty:NTF\\foo{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_if_empty:NTF a{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_if_empty:NTF\\c_empty_seq{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\let\\a\\l_s_seq\\seq_if_empty:NTF\\a{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\let\\a\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\seq_if_empty:NTF\\a{Y}{N}\\seq_if_empty:NTF\\l_s_seq{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\edef\\x{\\seq_if_empty:NTF\\l_s_seq{Y}{N}}\\x\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\edef\\x{\\seq_if_empty:NTF\\l_s_seq{Y}{N}}\\x\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{}\\seq_if_empty:NTF\\l_s_seq{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b}\\seq_if_empty:NTF\\l_s_seq{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\seq_clear:N\\l_s_seq\\seq_if_empty:NTF\\l_s_seq{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq{\\seq_put_right:Nn\\l_s_seq{A}\\seq_if_empty:NTF\\l_s_seq{Y}{N}}\\seq_if_empty:NTF\\l_s_seq{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq{\\seq_gput_right:Nn\\l_s_seq{A}}\\seq_if_empty:NTF\\l_s_seq{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq{\\seq_gput_right:Nn\\l_s_seq{A}\\seq_gclear:N\\l_s_seq}\\seq_if_empty:NTF\\l_s_seq{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_if_empty:NTF\\l_s_seq{\\seq_if_empty:NTF\\l_s_seq{Y}{N}}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_if_empty:NTF\\l_s_seq{}{N}X\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\seq_if_empty:NTF\\l_s_seq{Y}{}X\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\def\\c_empty_seq{A}\\def\\foo{A}\\seq_if_empty:NTF\\foo{Y}{N}\\seq_if_empty:NTF\\l_s_seq{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq{\\def\\c_empty_seq{A}\\seq_if_empty:NTF\\l_s_seq{Y}{N}}\\seq_if_empty:NTF\\l_s_seq{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\def\\yes{Y}\\def\\no{N}\\seq_if_empty:NTF\\l_s_seq{\\yes}{\\no}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\def\\danger{\\undefined}\\seq_if_empty:NTF\\l_s_seq{Y}{\\danger}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\let\\isempty\\seq_if_empty:NTF\\isempty\\l_s_seq{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\iftrue\\seq_if_empty:NTF\\l_s_seq{Y}{N}\\else Z\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\def\\test#1{\\seq_if_empty:NTF#1{Y}{N}}\\test\\l_s_seq\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_if_empty:NTF\\l_s_seq{\\int_eval:n{2+3}}{0}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_if_empty:NTF\\l_s_seq{\\tl_if_empty:nTF{}{Y}{N}}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_if_empty:NTF\\l_s_seq{a~b}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_left:Nn\\l_s_seq{{a,b}}\\seq_if_empty:NTF\\l_s_seq{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{}\\seq_if_empty:NTF\\l_s_seq{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_gset_from_clist:Nn\\l_s_seq{a}\\seq_gclear:N\\l_s_seq\\seq_if_empty:NTF\\l_s_seq{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\c_empty_seq\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\c_empty_seq\\c_empty_seq Y\\else N\\fi\\ExplSyntaxOff"
]) check(source)^]

fn safe_digestion(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 if(d.ast!=null) d.ast==(parse(source,{type:"latex"})^) else d.issue=="latex-digest-unsupported"
}
[for(source in ["\\begin{document}\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_if_empty:NTF\\l_s_seq{Y}{N}\\ExplSyntaxOff\\end{document}","\\begin{document}$\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_if_empty:NT\\l_s_seq{x}\\ExplSyntaxOff$\\end{document}"]) safe_digestion(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_seq_empty_main.tex","text")^;
let resources={local:{'script_seq_empty_child.tex':{data:input(root ++ "script_seq_empty_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_seq_empty_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;let n=input(root ++ "script_seq_empty_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==slice(n.messages,len(baseline),len(n.messages))^,diagnostics_match:r.diagnostic_records==n.diagnostics}
