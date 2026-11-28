import compare: ._tex_script_recovery
import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let native=input("test/lambda/latex/fixtures/tex_expl3_cs_exist_c_native.json","json")^;
let sources = [
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
  "\\ExplSyntaxOn\\ifx\\tl_if_exist:cTF\\cs_if_exist:cTF Y\\else N\\fi\\ifx\\tl_if_exist:cT\\cs_if_exist:cT Y\\else N\\fi\\ifx\\tl_if_exist:cF\\cs_if_exist:cF Y\\else N\\fi\\ifx\\tl_if_exist_p:c\\cs_if_exist_p:c Y\\else N\\fi\\ExplSyntaxOff"
];
compare.check_native_expl_expansions(sources,native.cases)^;


let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_cs_exist_c_main.tex","text")^;
let resources={local:{'script_cs_exist_c_child.tex':{data:input(root ++ "script_cs_exist_c_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_cs_exist_c_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;let n=native.local.tex;
{issue:r.issue,reconstruct_issue:b.issue,text_match:root ++ "script_cs_exist_c_main.tex"==native.local.source and b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}
