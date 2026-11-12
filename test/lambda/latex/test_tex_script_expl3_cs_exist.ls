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
  "\\ExplSyntaxOn\\meaning\\cs_if_exist:NTF/\\meaning\\cs_if_exist:NT/\\meaning\\cs_if_exist:NF/\\meaning\\cs_if_exist_p:N\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\show\\cs_if_exist:NTF\\show\\cs_if_exist_p:N\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_exist:NTF\\unknown{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_exist:NTF\\relax{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_exist:NTF\\scan_stop:{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\cs_if_exist:NTF\\foo{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_exist:NTF\\tex_let:D{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\foo\\relax\\cs_if_exist:NTF\\foo{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\foo\\unknown\\cs_if_exist:NTF\\foo{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\cs_if_exist:NT\\foo{Y}X\\cs_if_exist:NT\\unknown{N}X\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\cs_if_exist:NF\\foo{N}X\\cs_if_exist:NF\\unknown{Y}X\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\int_eval:n{\\cs_if_exist_p:N\\foo+\\cs_if_exist_p:N\\unknown}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\cs_if_exist:NTF\\foo{\\cs_if_exist:NTF\\unknown{N}{Y}}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\let\\q\\cs_if_exist:NTF\\q\\foo{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\edef\\z{\\cs_if_exist:NTF\\foo{Y}{N}}\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_exist:NTF\\cs_if_exist:NTF{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_exist:NTF\\s__seq{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_exist:NTF\\c_zero_int{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_exist:NTF\\c_true_bool{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_exist:NTF\\exp_stop_f:{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}{\\let\\foo\\relax\\cs_if_exist:NTF\\foo{Y}{N}}\\cs_if_exist:NTF\\foo{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\afterassignment X\\cs_if_exist:NTF\\foo{Y}{N}\\def\\bar{B}\\bar\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\foo\\unknown\\edef\\z{\\cs_if_exist:NTF\\foo{Y}{N}}\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\edef\\z{\\cs_if_exist_p:N\\unknown}\\meaning\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\int_eval:n{1+\\cs_if_exist_p:N\\foo}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\cs_if_exist:NTF\\foo{\\cs_if_exist:NF\\unknown{Y}}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\cs_if_exist:NTF\\foo{藏α}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\let\\f\\cs_if_exist:NF\\f\\foo{N}\\f\\unknown{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\if_cs_exist:N\\ifdefined Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_exist:NTF\\l_tmpa_tl{Y}{N}\\ExplSyntaxOff"
]) check(source)^]

fn guarded(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 d.ast==null and d.issue=="latex-digest-unsupported" and d.unsupported=="expl3 control-sequence existence source mapping"
}
[for(source in [
 "\\ExplSyntaxOn\\cs_if_exist:NTF\\unknown{\\def\\foo{Y}}{\\def\\foo{N}}\\ExplSyntaxOff\\begin{document}\\foo\\end{document}",
 "\\ExplSyntaxOn\\def\\foo{A}\\cs_if_exist:NTF\\foo{\\def\\bar{Y}}{\\def\\bar{N}}\\ExplSyntaxOff\\begin{document}\\bar\\end{document}",
 "\\begin{document}\\ExplSyntaxOn\\cs_if_exist:NTF\\unknown{Y}{N}\\ExplSyntaxOff\\end{document}",
 "\\ExplSyntaxOn\\let\\q\\cs_if_exist:NTF\\q\\unknown{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}"
]) guarded(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_cs_exist_main.tex","text")^;
let resources={local:{'script_cs_exist_child.tex':{data:input(root ++ "script_cs_exist_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_cs_exist_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;let n=input(root ++ "script_cs_exist_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}
