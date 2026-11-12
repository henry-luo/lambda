import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
fn check(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},10000)^;
 let n=parse(source,{type:"tex"})^;let b=reconstruct.reconstruct(r)^;
 {issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}
}
[for(source in [
  "\\ExplSyntaxOn\\def\\foo{A}\\tl_if_exist:NTF\\foo{Y}{N}\\tl_if_exist:NTF\\missing{N}{Y}\\tl_if_exist:NT\\foo{T}\\tl_if_exist:NF\\missing{F}\\int_eval:n{\\tl_if_exist_p:N\\foo+\\tl_if_exist_p:N\\missing}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\tl_if_exist:NTF\\cs_if_exist:NTF Y\\else N\\fi\\ifx\\tl_if_exist:NT\\cs_if_exist:NT Y\\else N\\fi\\ifx\\tl_if_exist:NF\\cs_if_exist:NF Y\\else N\\fi\\ifx\\tl_if_exist_p:N\\cs_if_exist_p:N Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\q\\tl_if_exist:NTF\\def\\cs_if_exist:NTF#1#2#3{BAD}\\q\\undefined{N}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\seq_if_exist:NTF\\foo{Y}{N}\\seq_if_exist:NTF\\missing{N}{Y}\\seq_if_exist:NT\\foo{T}\\seq_if_exist:NF\\missing{F}\\int_eval:n{\\seq_if_exist_p:N\\foo+\\seq_if_exist_p:N\\missing}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\seq_if_exist:NTF\\cs_if_exist:NTF Y\\else N\\fi\\ifx\\seq_if_exist:NT\\cs_if_exist:NT Y\\else N\\fi\\ifx\\seq_if_exist:NF\\cs_if_exist:NF Y\\else N\\fi\\ifx\\seq_if_exist_p:N\\cs_if_exist_p:N Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\q\\seq_if_exist:NTF\\def\\cs_if_exist:NTF#1#2#3{BAD}\\q\\undefined{N}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\int_if_exist:NTF\\foo{Y}{N}\\int_if_exist:NTF\\missing{N}{Y}\\int_if_exist:NT\\foo{T}\\int_if_exist:NF\\missing{F}\\int_eval:n{\\int_if_exist_p:N\\foo+\\int_if_exist_p:N\\missing}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\int_if_exist:NTF\\cs_if_exist:NTF Y\\else N\\fi\\ifx\\int_if_exist:NT\\cs_if_exist:NT Y\\else N\\fi\\ifx\\int_if_exist:NF\\cs_if_exist:NF Y\\else N\\fi\\ifx\\int_if_exist_p:N\\cs_if_exist_p:N Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\q\\int_if_exist:NTF\\def\\cs_if_exist:NTF#1#2#3{BAD}\\q\\undefined{N}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\bool_if_exist:NTF\\foo{Y}{N}\\bool_if_exist:NTF\\missing{N}{Y}\\bool_if_exist:NT\\foo{T}\\bool_if_exist:NF\\missing{F}\\int_eval:n{\\bool_if_exist_p:N\\foo+\\bool_if_exist_p:N\\missing}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\bool_if_exist:NTF\\cs_if_exist:NTF Y\\else N\\fi\\ifx\\bool_if_exist:NT\\cs_if_exist:NT Y\\else N\\fi\\ifx\\bool_if_exist:NF\\cs_if_exist:NF Y\\else N\\fi\\ifx\\bool_if_exist_p:N\\cs_if_exist_p:N Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\q\\bool_if_exist:NTF\\def\\cs_if_exist:NTF#1#2#3{BAD}\\q\\undefined{N}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\clist_if_exist:NTF\\foo{Y}{N}\\clist_if_exist:NTF\\missing{N}{Y}\\clist_if_exist:NT\\foo{T}\\clist_if_exist:NF\\missing{F}\\int_eval:n{\\clist_if_exist_p:N\\foo+\\clist_if_exist_p:N\\missing}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\clist_if_exist:NTF\\cs_if_exist:NTF Y\\else N\\fi\\ifx\\clist_if_exist:NT\\cs_if_exist:NT Y\\else N\\fi\\ifx\\clist_if_exist:NF\\cs_if_exist:NF Y\\else N\\fi\\ifx\\clist_if_exist_p:N\\cs_if_exist_p:N Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\q\\clist_if_exist:NTF\\def\\cs_if_exist:NTF#1#2#3{BAD}\\q\\undefined{N}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\prop_if_exist:NTF\\foo{Y}{N}\\prop_if_exist:NTF\\missing{N}{Y}\\prop_if_exist:NT\\foo{T}\\prop_if_exist:NF\\missing{F}\\int_eval:n{\\prop_if_exist_p:N\\foo+\\prop_if_exist_p:N\\missing}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\prop_if_exist:NTF\\cs_if_exist:NTF Y\\else N\\fi\\ifx\\prop_if_exist:NT\\cs_if_exist:NT Y\\else N\\fi\\ifx\\prop_if_exist:NF\\cs_if_exist:NF Y\\else N\\fi\\ifx\\prop_if_exist_p:N\\cs_if_exist_p:N Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\q\\prop_if_exist:NTF\\def\\cs_if_exist:NTF#1#2#3{BAD}\\q\\undefined{N}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\str_if_exist:NTF\\foo{Y}{N}\\str_if_exist:NTF\\missing{N}{Y}\\str_if_exist:NT\\foo{T}\\str_if_exist:NF\\missing{F}\\int_eval:n{\\str_if_exist_p:N\\foo+\\str_if_exist_p:N\\missing}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\str_if_exist:NTF\\cs_if_exist:NTF Y\\else N\\fi\\ifx\\str_if_exist:NT\\cs_if_exist:NT Y\\else N\\fi\\ifx\\str_if_exist:NF\\cs_if_exist:NF Y\\else N\\fi\\ifx\\str_if_exist_p:N\\cs_if_exist_p:N Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\q\\str_if_exist:NTF\\def\\cs_if_exist:NTF#1#2#3{BAD}\\q\\undefined{N}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\dim_if_exist:NTF\\foo{Y}{N}\\dim_if_exist:NTF\\missing{N}{Y}\\dim_if_exist:NT\\foo{T}\\dim_if_exist:NF\\missing{F}\\int_eval:n{\\dim_if_exist_p:N\\foo+\\dim_if_exist_p:N\\missing}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\dim_if_exist:NTF\\cs_if_exist:NTF Y\\else N\\fi\\ifx\\dim_if_exist:NT\\cs_if_exist:NT Y\\else N\\fi\\ifx\\dim_if_exist:NF\\cs_if_exist:NF Y\\else N\\fi\\ifx\\dim_if_exist_p:N\\cs_if_exist_p:N Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\q\\dim_if_exist:NTF\\def\\cs_if_exist:NTF#1#2#3{BAD}\\q\\undefined{N}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\tl_if_exist:NTF/\\meaning\\tl_if_exist_p:N\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\seq_if_exist:NTF/\\meaning\\seq_if_exist_p:N\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\int_if_exist:NTF/\\meaning\\int_if_exist_p:N\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\bool_if_exist:NTF/\\meaning\\bool_if_exist_p:N\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\clist_if_exist:NTF/\\meaning\\clist_if_exist_p:N\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\prop_if_exist:NTF/\\meaning\\prop_if_exist_p:N\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\str_if_exist:NTF/\\meaning\\str_if_exist_p:N\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\dim_if_exist:NTF/\\meaning\\dim_if_exist_p:N\\ExplSyntaxOff"
]) check(source)^]

fn guarded(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 d.ast==null and d.issue=="latex-digest-unsupported" and d.unsupported=="expl3 control-sequence existence source mapping"
}
[for(source in ["\\ExplSyntaxOn\\tl_if_exist:NTF\\unknown{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\ExplSyntaxOn\\seq_if_exist:NTF\\unknown{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\ExplSyntaxOn\\int_if_exist:NTF\\unknown{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\ExplSyntaxOn\\bool_if_exist:NTF\\unknown{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\ExplSyntaxOn\\clist_if_exist:NTF\\unknown{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\ExplSyntaxOn\\prop_if_exist:NTF\\unknown{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\ExplSyntaxOn\\str_if_exist:NTF\\unknown{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\ExplSyntaxOn\\dim_if_exist:NTF\\unknown{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\ExplSyntaxOn\\let\\q\\tl_if_exist:NTF\\q\\unknown{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}","\\ExplSyntaxOn\\let\\q\\seq_if_exist:NTF\\q\\unknown{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}"]) guarded(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_exist_alias_main.tex","text")^;
let resources={local:{'script_exist_alias_child.tex':{data:input(root ++ "script_exist_alias_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_exist_alias_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;let n=input(root ++ "script_exist_alias_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}
