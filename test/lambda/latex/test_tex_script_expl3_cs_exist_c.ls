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
  "\\ExplSyntaxOn\\meaning\\cs_if_exist:cTF/\\meaning\\cs_if_exist:cT/\\meaning\\cs_if_exist:cF/\\meaning\\cs_if_exist_p:c\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\show\\cs_if_exist:cTF\\show\\cs_if_exist_p:c\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_exist:cTF{missing}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_exist:cTF{relax}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_exist:cTF{scan_stop:}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\cs_if_exist:cTF{foo}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\def\\name{foo}\\cs_if_exist:cTF{\\name}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\cs_if_exist:cT{foo}{Y}\\cs_if_exist:cF{missing}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\int_eval:n{\\cs_if_exist_p:c{foo}+\\cs_if_exist_p:c{missing}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_exist:cTF{missing}{Y}{N}\\ifcsname missing\\endcsname Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\expandafter\\let\\csname foo\\endcsname\\relax\\cs_if_exist:cTF{foo}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\edef\\z{\\cs_if_exist:cTF{foo}{Y}{N}}\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\q\\cs_if_exist:cTF\\q{missing}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\afterassignment X\\cs_if_exist:cTF{foo}{Y}{N}\\def\\bar{B}\\bar\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_exist:cTF{c_zero_int}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_exist:cTF{l_tmpa_tl}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\tl_if_exist:cTF{foo}{Y}{N}\\tl_if_exist:cT{foo}{T}\\tl_if_exist:cF{missing}{F}\\int_eval:n{\\tl_if_exist_p:c{foo}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\tl_if_exist:cTF\\cs_if_exist:cTF Y\\else N\\fi\\ifx\\tl_if_exist:cT\\cs_if_exist:cT Y\\else N\\fi\\ifx\\tl_if_exist:cF\\cs_if_exist:cF Y\\else N\\fi\\ifx\\tl_if_exist_p:c\\cs_if_exist_p:c Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\seq_if_exist:cTF{foo}{Y}{N}\\seq_if_exist:cT{foo}{T}\\seq_if_exist:cF{missing}{F}\\int_eval:n{\\seq_if_exist_p:c{foo}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\seq_if_exist:cTF\\cs_if_exist:cTF Y\\else N\\fi\\ifx\\seq_if_exist:cT\\cs_if_exist:cT Y\\else N\\fi\\ifx\\seq_if_exist:cF\\cs_if_exist:cF Y\\else N\\fi\\ifx\\seq_if_exist_p:c\\cs_if_exist_p:c Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\int_if_exist:cTF{foo}{Y}{N}\\int_if_exist:cT{foo}{T}\\int_if_exist:cF{missing}{F}\\int_eval:n{\\int_if_exist_p:c{foo}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\int_if_exist:cTF\\cs_if_exist:cTF Y\\else N\\fi\\ifx\\int_if_exist:cT\\cs_if_exist:cT Y\\else N\\fi\\ifx\\int_if_exist:cF\\cs_if_exist:cF Y\\else N\\fi\\ifx\\int_if_exist_p:c\\cs_if_exist_p:c Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\bool_if_exist:cTF{foo}{Y}{N}\\bool_if_exist:cT{foo}{T}\\bool_if_exist:cF{missing}{F}\\int_eval:n{\\bool_if_exist_p:c{foo}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\bool_if_exist:cTF\\cs_if_exist:cTF Y\\else N\\fi\\ifx\\bool_if_exist:cT\\cs_if_exist:cT Y\\else N\\fi\\ifx\\bool_if_exist:cF\\cs_if_exist:cF Y\\else N\\fi\\ifx\\bool_if_exist_p:c\\cs_if_exist_p:c Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\clist_if_exist:cTF{foo}{Y}{N}\\clist_if_exist:cT{foo}{T}\\clist_if_exist:cF{missing}{F}\\int_eval:n{\\clist_if_exist_p:c{foo}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\clist_if_exist:cTF\\cs_if_exist:cTF Y\\else N\\fi\\ifx\\clist_if_exist:cT\\cs_if_exist:cT Y\\else N\\fi\\ifx\\clist_if_exist:cF\\cs_if_exist:cF Y\\else N\\fi\\ifx\\clist_if_exist_p:c\\cs_if_exist_p:c Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\prop_if_exist:cTF{foo}{Y}{N}\\prop_if_exist:cT{foo}{T}\\prop_if_exist:cF{missing}{F}\\int_eval:n{\\prop_if_exist_p:c{foo}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\prop_if_exist:cTF\\cs_if_exist:cTF Y\\else N\\fi\\ifx\\prop_if_exist:cT\\cs_if_exist:cT Y\\else N\\fi\\ifx\\prop_if_exist:cF\\cs_if_exist:cF Y\\else N\\fi\\ifx\\prop_if_exist_p:c\\cs_if_exist_p:c Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\str_if_exist:cTF{foo}{Y}{N}\\str_if_exist:cT{foo}{T}\\str_if_exist:cF{missing}{F}\\int_eval:n{\\str_if_exist_p:c{foo}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\str_if_exist:cTF\\cs_if_exist:cTF Y\\else N\\fi\\ifx\\str_if_exist:cT\\cs_if_exist:cT Y\\else N\\fi\\ifx\\str_if_exist:cF\\cs_if_exist:cF Y\\else N\\fi\\ifx\\str_if_exist_p:c\\cs_if_exist_p:c Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\dim_if_exist:cTF{foo}{Y}{N}\\dim_if_exist:cT{foo}{T}\\dim_if_exist:cF{missing}{F}\\int_eval:n{\\dim_if_exist_p:c{foo}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\dim_if_exist:cTF\\cs_if_exist:cTF Y\\else N\\fi\\ifx\\dim_if_exist:cT\\cs_if_exist:cT Y\\else N\\fi\\ifx\\dim_if_exist:cF\\cs_if_exist:cF Y\\else N\\fi\\ifx\\dim_if_exist_p:c\\cs_if_exist_p:c Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\def\\name{foo}\\cs_if_exist:cTF{\\name}{\\cs_if_exist:cF{missing}{Y}}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\let\\q\\cs_if_exist:cTF\\def\\cs_if_exist:cTF#1#2#3{BAD}\\q{foo}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\cs_if_exist:cTF{foo}{藏α}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}{\\let\\foo\\relax\\cs_if_exist:cTF{foo}{Y}{N}}\\cs_if_exist:cTF{foo}{Y}{N}\\ExplSyntaxOff"
]) check(source)^]

fn guarded(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 d.ast==null and d.issue=="latex-digest-unsupported" and d.unsupported=="expl3 control-sequence existence source mapping"
}
[for(source in ["\\ExplSyntaxOn\\cs_if_exist:cTF{unknown}{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\ExplSyntaxOn\\tl_if_exist:cTF{unknown}{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\ExplSyntaxOn\\seq_if_exist:cTF{unknown}{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\ExplSyntaxOn\\int_if_exist:cTF{unknown}{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\ExplSyntaxOn\\bool_if_exist:cTF{unknown}{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\ExplSyntaxOn\\clist_if_exist:cTF{unknown}{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\ExplSyntaxOn\\prop_if_exist:cTF{unknown}{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\ExplSyntaxOn\\str_if_exist:cTF{unknown}{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\ExplSyntaxOn\\dim_if_exist:cTF{unknown}{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\ExplSyntaxOn\\let\\q\\cs_if_exist:cTF\\q{unknown}{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}"]) guarded(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_cs_exist_c_main.tex","text")^;
let resources={local:{'script_cs_exist_c_child.tex':{data:input(root ++ "script_cs_exist_c_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_cs_exist_c_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;let n=input(root ++ "script_cs_exist_c_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}
