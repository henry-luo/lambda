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
  "\\ExplSyntaxOn\\meaning\\cs_if_eq:NNTF/\\meaning\\cs_if_eq:NNT/\\meaning\\cs_if_eq:NNF/\\meaning\\cs_if_eq_p:NN\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\show\\cs_if_eq:NNTF\\show\\cs_if_eq_p:NN\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_eq:NNTF\\foo\\bar{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_eq:NNTF\\foo\\relax{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_eq:NNTF\\scan_stop:\\relax{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\def\\bar{A}\\cs_if_eq:NNTF\\foo\\bar{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\def\\bar{B}\\cs_if_eq:NNTF\\foo\\bar{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\let\\bar\\foo\\cs_if_eq:NNTF\\foo\\bar{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\protected\\def\\bar{A}\\cs_if_eq:NNTF\\foo\\bar{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo#1{#1}\\def\\bar#1{#1}\\cs_if_eq:NNT\\foo\\bar{Y}\\cs_if_eq:NNF\\foo\\relax{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\let\\bar\\foo\\int_eval:n{\\cs_if_eq_p:NN\\foo\\bar+\\cs_if_eq_p:NN\\foo\\relax}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\cs_if_eq:NNTF\\foo\\foo{\\cs_if_eq:NNTF\\foo\\relax{N}{Y}}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\let\\q\\cs_if_eq:NNTF\\q\\foo\\foo{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\edef\\z{\\cs_if_eq:NNTF\\foo\\foo{Y}{N}}\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\afterassignment X\\cs_if_eq:NNTF\\foo\\foo{Y}{N}\\def\\bar{B}\\bar\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_eq:NNTF\\c_zero_int\\c_false_bool{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_eq:NNTF\\tex_let:D\\let{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\cs_if_eq:NNTF\\foo\\foo{藏α}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}{\\let\\bar\\foo\\cs_if_eq:NNTF\\foo\\bar{Y}{N}}\\cs_if_eq:NNTF\\foo\\bar{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\long\\def\\bar{A}\\cs_if_eq:NNTF\\foo\\bar{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\catcode`\\!=11 \\def\\foo{!}\\catcode`\\!=12 \\def\\bar{!}\\cs_if_eq:NNTF\\foo\\bar{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo#1{#1}\\def\\bar#1{A#1}\\cs_if_eq:NNTF\\foo\\bar{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\countdef\\a=200 \\countdef\\b=200 \\cs_if_eq:NNTF\\a\\b{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\countdef\\a=200 \\countdef\\b=201 \\cs_if_eq:NNTF\\a\\b{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_eq:NNTF\\exp_stop_f:\\exp_stop_f:{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\edef\\z{\\cs_if_eq_p:NN\\foo\\foo}\\meaning\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\foo{A}\\let\\q\\cs_if_eq:NNF\\q\\foo\\relax{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_eq:NNTF\\unknown\\undefined{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_eq:NNTF\\c_zero_int\\c_one_int{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\cs_if_eq:NNTF\\cs_if_eq:NNTF\\cs_if_eq:NNTF{Y}{N}\\ExplSyntaxOff"
]) check(source)^]

fn guarded(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 d.ast==null and d.issue=="latex-digest-unsupported" and d.unsupported=="expl3 control-sequence equality source mapping"
}
[for(source in [
 "\\ExplSyntaxOn\\cs_if_eq:NNTF\\foo\\bar{\\def\\z{Y}}{\\def\\z{N}}\\ExplSyntaxOff\\begin{document}\\z\\end{document}",
 "\\ExplSyntaxOn\\let\\q\\cs_if_eq:NNTF\\q\\foo\\bar{Y}{N}\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\begin{document}\\ExplSyntaxOn\\cs_if_eq:NNTF\\foo\\bar{Y}{N}\\ExplSyntaxOff\\end{document}",
 "\\ExplSyntaxOn\\int_eval:n{\\cs_if_eq_p:NN\\foo\\bar}\\ExplSyntaxOff\\begin{document}Hello\\end{document}"
]) guarded(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_cs_eq_main.tex","text")^;
let resources={local:{'script_cs_eq_child.tex':{data:input(root ++ "script_cs_eq_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_cs_eq_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;let n=input(root ++ "script_cs_eq_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}
