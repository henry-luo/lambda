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
  "\\ExplSyntaxOn\\meaning\\exp_args:Nc/\\meaning\\exp_args:NNc/\\meaning\\exp_args:Ncc\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\cs_if_eq:cNTF/\\meaning\\cs_if_eq:NcTF/\\meaning\\cs_if_eq:ccTF\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\show\\cs_if_eq:cNTF\\show\\cs_if_eq_p:cc\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\let\\bar\\foo\\cs_if_eq:cNTF{foo}\\bar{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\let\\bar\\foo\\cs_if_eq:NcTF\\foo{bar}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\let\\bar\\foo\\cs_if_eq:ccTF{foo}{bar}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\def\\bar{B}\\cs_if_eq:cNF{foo}\\bar{Y}\\cs_if_eq:NcF\\foo{bar}{Y}\\cs_if_eq:ccF{foo}{bar}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\let\\bar\\foo\\cs_if_eq:cNT{foo}\\bar{Y}\\cs_if_eq:NcT\\foo{bar}{Y}\\cs_if_eq:ccT{foo}{bar}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\let\\bar\\foo\\int_eval:n{\\cs_if_eq_p:cN{foo}\\bar+\\cs_if_eq_p:Nc\\foo{bar}+\\cs_if_eq_p:cc{foo}{bar}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_eq:cNTF{missing}\\relax{Y}{N}\\ifcsname missing\\endcsname Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_eq:ccTF{firstmissing}{secondmissing}{Y}{N}\\ifcsname firstmissing\\endcsname Y\\else N\\fi\\ifcsname secondmissing\\endcsname Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\def\\name{foo}\\cs_if_eq:cNTF{\\name}\\foo{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\let\\q\\cs_if_eq:ccTF\\q{foo}{foo}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\edef\\z{\\cs_if_eq:cNTF{foo}\\foo{Y}{N}}\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\afterassignment X\\cs_if_eq:ccTF{foo}{foo}{Y}{N}\\def\\bar{B}\\bar\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\meaning\\cs_if_eq:cNTF/\\cs_if_eq:cNTF{foo}\\foo{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\meaning\\cs_if_eq:cNT/\\cs_if_eq:cNT{foo}\\foo{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\meaning\\cs_if_eq:cNF/\\cs_if_eq:cNF{foo}\\foo{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\meaning\\cs_if_eq_p:cN/\\int_eval:n{\\cs_if_eq_p:cN{foo}\\foo}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\meaning\\cs_if_eq:NcTF/\\cs_if_eq:NcTF\\foo{foo}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\meaning\\cs_if_eq:NcT/\\cs_if_eq:NcT\\foo{foo}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\meaning\\cs_if_eq:NcF/\\cs_if_eq:NcF\\foo{foo}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\meaning\\cs_if_eq_p:Nc/\\int_eval:n{\\cs_if_eq_p:Nc\\foo{foo}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\meaning\\cs_if_eq:ccTF/\\cs_if_eq:ccTF{foo}{foo}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\meaning\\cs_if_eq:ccT/\\cs_if_eq:ccT{foo}{foo}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\meaning\\cs_if_eq:ccF/\\cs_if_eq:ccF{foo}{foo}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\meaning\\cs_if_eq_p:cc/\\int_eval:n{\\cs_if_eq_p:cc{foo}{foo}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\def\\name{foo}\\cs_if_eq:ccTF{\\name}{foo}{藏α}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\q\\cs_if_eq:ccTF\\q{missing}{missing}{Y}{N}\\ExplSyntaxOff"
]) check(source)^]

fn guarded(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 d.ast==null and d.issue=="latex-digest-unsupported" and d.unsupported=="expl3 control-sequence equality source mapping"
}
[for(source in [
 "\\ExplSyntaxOn\\cs_if_eq:cNTF{foo}\\bar{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\cs_if_eq:NcTF\\foo{bar}{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\cs_if_eq:ccTF{foo}{bar}{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\let\\q\\cs_if_eq:ccTF\\q{foo}{bar}{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}"
]) guarded(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_cs_eq_c_main.tex","text")^;
let resources={local:{'script_cs_eq_c_child.tex':{data:input(root ++ "script_cs_eq_c_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_cs_eq_c_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;let n=input(root ++ "script_cs_eq_c_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}
