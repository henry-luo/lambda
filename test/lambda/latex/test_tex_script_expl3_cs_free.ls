import compare: ._tex_script_recovery
import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
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
  "\\ExplSyntaxOn\\def\\foo{A}\\cs_if_free:cF{foo}{Y}\\cs_if_free:cT{missing}{Y}\\ExplSyntaxOff"
]) compare.check_expansion(source,{script_expl3:true})^];


let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_cs_free_main.tex","text")^;
let resources={local:{'script_cs_free_child.tex':{data:input(root ++ "script_cs_free_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_cs_free_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;let n=input(root ++ "script_cs_free_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}
