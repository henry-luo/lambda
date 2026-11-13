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
  "\\ExplSyntaxOn\\tl_new:N\\l_x_tl\\tl_if_empty:cTF{l_x_tl}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\tl_if_empty:cTF\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_x_tl\\tl_if_empty:cT{l_x_tl}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\tl_if_empty:cT\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_x_tl\\tl_if_empty:cF{l_x_tl}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\tl_if_empty:cF\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_x_seq\\seq_if_empty:cTF{l_x_seq}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\seq_if_empty:cTF\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_x_seq\\seq_if_empty:cT{l_x_seq}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\seq_if_empty:cT\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_x_seq\\seq_if_empty:cF{l_x_seq}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\seq_if_empty:cF\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\show\\tl_if_empty:cTF\\show\\seq_if_empty:cTF\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_x_tl\\tl_set:Nn\\l_x_tl{A}\\tl_if_empty:cTF{l_x_tl}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_x_seq\\seq_put_right:Nn\\l_x_seq{A}\\seq_if_empty:cTF{l_x_seq}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_if_empty:cTF{missing}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_if_empty:cTF{missing}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_x_tl\\def\\prefix{l_x}\\tl_if_empty:cTF{\\prefix _tl}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_x_seq\\def\\prefix{l_x}\\seq_if_empty:cTF{\\prefix _seq}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_x_tl\\let\\q\\tl_if_empty:cTF\\q{l_x_tl}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_x_seq\\edef\\z{\\seq_if_empty:cTF{l_x_seq}{Y}{N}}\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_x_tl\\afterassignment X\\tl_if_empty:cTF{l_x_tl}{Y}{N}\\def\\z{B}\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_x_seq\\seq_if_empty:cTF{l_x_seq}{\\tl_if_empty:cTF{l_tmpa_tl}{Y}{N}}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_x_tl\\tl_set:Nn\\l_x_tl{}\\tl_if_empty:cTF{l_x_tl}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_x_tl\\tl_set:Nn\\l_x_tl{~}\\tl_if_empty:cTF{l_x_tl}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_x_seq\\seq_put_right:Nn\\l_x_seq{}\\seq_if_empty:cTF{l_x_seq}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq}\\seq_if_empty:cTF{s}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\l_x_tl\\relax\\tl_if_empty:cTF{l_x_tl}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\l_x_seq\\relax\\seq_if_empty:cTF{l_x_seq}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_if_empty:cTF{missing}{Y}{N}\\ifcsname missing\\endcsname Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_x_seq\\afterassignment X\\seq_if_empty:cTF{l_x_seq}{Y}{N}\\def\\z{B}\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_x_tl\\edef\\z{\\tl_if_empty:cTF{l_x_tl}{Y}{N}}\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_x_seq\\let\\q\\seq_if_empty:cTF\\q{l_x_seq}{藏α}{N}\\ExplSyntaxOff"
]) check(source)^]

fn guarded(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 d.ast==null and d.issue=="latex-digest-unsupported" and d.unsupported=="expl3 dynamic read source mapping"
}
[for(source in ["\\ExplSyntaxOn\\tl_if_empty:cTF{missing}{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\ExplSyntaxOn\\tl_if_empty:cT{missing}{Y}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\ExplSyntaxOn\\tl_if_empty:cF{missing}{Y}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\ExplSyntaxOn\\seq_if_empty:cTF{missing}{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\ExplSyntaxOn\\seq_if_empty:cT{missing}{Y}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\ExplSyntaxOn\\seq_if_empty:cF{missing}{Y}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\ExplSyntaxOn\\let\\q\\tl_if_empty:cTF\\q{missing}{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}"]) guarded(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_empty_c_main.tex","text")^;
let resources={local:{'script_empty_c_child.tex':{data:input(root ++ "script_empty_c_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_empty_c_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;let n=input(root ++ "script_empty_c_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}
