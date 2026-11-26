import compare: ._tex_script_recovery
import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let native=input("test/lambda/latex/fixtures/tex_expl3_cs_exist_native.json","json")^;
let sources = [
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
  "\\ExplSyntaxOn\\def\\foo{A}\\edef\\z{\\cs_if_exist:NTF\\foo{Y}{N}}\\z\\ExplSyntaxOff"
];
compare.check_native_expl_expansions(sources,native.cases)^;


let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_cs_exist_main.tex","text")^;
let resources={local:{'script_cs_exist_child.tex':{data:input(root ++ "script_cs_exist_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_cs_exist_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;let n=native.local.tex;
{issue:r.issue,reconstruct_issue:b.issue,text_match:root ++ "script_cs_exist_main.tex"==native.local.source and b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}
