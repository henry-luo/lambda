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
  "\\ExplSyntaxOn\\meaning\\cs_if_free:NTF/\\meaning\\cs_if_free:NT/\\meaning\\cs_if_free:NF/\\meaning\\cs_if_free_p:N\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\cs_if_free:cTF/\\meaning\\cs_if_free:cT/\\meaning\\cs_if_free:cF/\\meaning\\cs_if_free_p:c\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\show\\cs_if_free:NTF\\show\\cs_if_free:cTF\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_free:NTF\\unknown{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_free:NTF\\relax{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_free:NTF\\scan_stop:{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\cs_if_free:NTF\\foo{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\foo\\relax\\cs_if_free:NTF\\foo{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\foo\\unknown\\cs_if_free:NTF\\foo{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\cs_if_free:NF\\foo{Y}\\cs_if_free:NT\\unknown{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\int_eval:n{\\cs_if_free_p:N\\foo+\\cs_if_free_p:N\\unknown}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_free:cTF{missing}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_free:cTF{relax}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\cs_if_free:cTF{foo}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\def\\name{foo}\\cs_if_free:cTF{\\name}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\cs_if_free:cF{foo}{Y}\\cs_if_free:cT{missing}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\int_eval:n{\\cs_if_free_p:c{foo}+\\cs_if_free_p:c{missing}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_free:cTF{missing}{Y}{N}\\ifcsname missing\\endcsname Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\q\\cs_if_free:cTF\\q{missing}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\edef\\z{\\cs_if_free:NTF\\foo{Y}{N}}\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_free:NTF\\cs_if_free:NTF{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_free:NTF\\s__seq{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_free:NTF\\c_zero_int{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_free:NTF\\exp_stop_f:{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}{\\let\\foo\\relax\\cs_if_free:NTF\\foo{Y}{N}}\\cs_if_free:NTF\\foo{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\afterassignment X\\cs_if_free:NTF\\foo{N}{Y}\\def\\bar{B}\\bar\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\edef\\z{\\cs_if_free_p:N\\unknown}\\meaning\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\cs_if_free:cTF{foo}{N}{\\cs_if_free:NT\\missing{Y}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_free:cTF{missing}{藏α}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\let\\q\\cs_if_free:NF\\q\\foo{Y}\\q\\missing{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\int_eval:n{1+\\cs_if_free_p:c{foo}+\\cs_if_free_p:c{missing}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_free:cTF{l_tmpa_tl}{Y}{N}\\ExplSyntaxOff"
]) check(source)^]

fn guarded(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 d.ast==null and d.issue=="latex-digest-unsupported" and d.unsupported=="expl3 control-sequence existence source mapping"
}
[for(source in [
 "\\ExplSyntaxOn\\cs_if_free:NTF\\unknown{\\def\\foo{Y}}{\\def\\foo{N}}\\ExplSyntaxOff\\begin{document}\\foo\\end{document}",
 "\\ExplSyntaxOn\\cs_if_free:cTF{unknown}{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\let\\q\\cs_if_free:NTF\\q\\unknown{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\let\\q\\cs_if_free:cTF\\q{unknown}{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}"
]) guarded(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_cs_free_main.tex","text")^;
let resources={local:{'script_cs_free_child.tex':{data:input(root ++ "script_cs_free_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_cs_free_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;let n=input(root ++ "script_cs_free_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}
