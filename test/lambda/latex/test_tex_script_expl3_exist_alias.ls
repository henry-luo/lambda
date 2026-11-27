import compare: ._tex_script_recovery
import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let sources = [
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
  "\\ExplSyntaxOn\\def\\foo{A}\\prop_if_exist:NTF\\foo{Y}{N}\\prop_if_exist:NTF\\missing{N}{Y}\\prop_if_exist:NT\\foo{T}\\prop_if_exist:NF\\missing{F}\\int_eval:n{\\prop_if_exist_p:N\\foo+\\prop_if_exist_p:N\\missing}\\ExplSyntaxOff"
];
let native=input("test/lambda/latex/fixtures/tex_expl3_exist_alias_native.json","json")^;
// retain a fresh first expansion and compare every source with its native capture.
compare.check_native_expl_expansions(sources,native.cases)^;


let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_exist_alias_main.tex","text")^;
let child=input(root ++ "script_exist_alias_child.tex","text")^;
let resources={local:{'script_exist_alias_child.tex':{data:child}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_exist_alias_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;let n=native.local.tex;
let bound=source==native.local.source and child==native.local.child;
{issue:r.issue,reconstruct_issue:b.issue,text_match:bound and b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}
