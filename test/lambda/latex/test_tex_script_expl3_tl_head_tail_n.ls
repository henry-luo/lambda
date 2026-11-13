import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import expansion: lambda.doc.latex.tex_engine.expansion
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let baseline=parse("",{type:"tex"})^.messages;
fn check(body, constructor = false) any^ {
 let source="\\ExplSyntaxOn" ++ body ++ "\\ExplSyntaxOff";
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,constructor_argument_mode:constructor,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let n=parse(source,{type:"tex"})^;let b=reconstruct.reconstruct(r)^;
 {body:body,issue:r.issue,unsupported:r.unsupported,reconstruct_issue:b.issue,text_match:if(r.issue==null)b.text==n.text else null,
 messages_match:r.messages==slice(n.messages,len(baseline),len(n.messages))^,diagnostics_match:r.diagnostic_records==n.diagnostics}
}
[for (body in [
 "\\tl_set:Nn\\l_tmpa_tl{abc}\\tl_head:N\\l_tmpa_tl/\\tl_tail:N\\l_tmpa_tl",
 "\\tl_set:Nn\\l_tmpa_tl{}\\tl_head:N\\l_tmpa_tl/\\tl_tail:N\\l_tmpa_tl",
 "\\tl_set:Nn\\l_tmpa_tl{{ab}cd}\\tl_head:N\\l_tmpa_tl/\\tl_tail:N\\l_tmpa_tl",
 "\\tl_set:Nn\\l_tmpa_tl{~abc}\\tl_head:N\\l_tmpa_tl/\\tl_tail:N\\l_tmpa_tl",
 "\\tl_set:Nn\\l_tmpa_tl{~~}\\tl_head:N\\l_tmpa_tl/\\tl_tail:N\\l_tmpa_tl",
 "\\tl_set:Nn\\l_tmpa_tl{éclair}\\tl_head:N\\l_tmpa_tl/\\tl_tail:N\\l_tmpa_tl",
 "\\tl_set:Nn\\l_tmpa_tl{abc}\\edef\\x{\\tl_head:N\\l_tmpa_tl}\\meaning\\x",
 "\\tl_set:Nn\\l_tmpa_tl{abc}\\edef\\x{\\tl_tail:N\\l_tmpa_tl}\\meaning\\x",
 "\\tl_set:Nn\\l_tmpa_tl{abc}{\\tl_set:Nn\\l_tmpa_tl{xyz}\\tl_head:N\\l_tmpa_tl}\\tl_tail:N\\l_tmpa_tl",
 "\\tl_set:Nn\\l_tmpa_tl{abc}\\let\\h\\tl_head:N\\h\\l_tmpa_tl",
 "\\tl_set:Nn\\l_tmpa_tl{abc}\\let\\h\\tl_tail:N\\h\\l_tmpa_tl",
 "\\tl_set:Nn\\l_tmpa_tl{abc}\\if\\tl_head:N\\l_tmpa_tl a Y\\else N\\fi",
 "\\meaning\\exp_args:No/\\meaning\\tl_head:N/\\meaning\\tl_tail:N",
 "\\def\\x{abc}\\exp_args:No\\tl_head:n{\\x}",
 "\\def\\x{abc}\\exp_args:No\\tl_tail:n{\\x}",
 "\\def\\x{abc}\\exp_args:No\\use:n{\\x}", "\\exp_args:No\\use:n{{ab}c}",
 "\\def\\x{abc}\\tl_head:N\\x/\\tl_tail:N\\x",
 "\\def\\x{\\y}\\def\\y{abc}\\tl_head:N\\x/\\tl_tail:N\\x",
 "\\tl_set:Nn\\l_tmpa_tl{\\textbf{X}Y}\\tl_head:N\\l_tmpa_tl",
 "\\tl_set:Nn\\l_tmpa_tl{a\\textbf{X}}\\tl_tail:N\\l_tmpa_tl",
 "\\tl_set:Nn\\l_tmpa_tl{abc}\\begin{document}\\textbf{\\tl_head:N\\l_tmpa_tl}\\end{document}"
]) check(body)^];

[for (body in ["\\tl_set:Nn\\l_tmpa_tl{abc}\\begin{document}\\textbf{\\tl_head:N\\l_tmpa_tl}\\end{document}",
 "\\tl_set:Nn\\l_tmpa_tl{abc}\\begin{document}\\section{\\tl_tail:N\\l_tmpa_tl}X\\end{document}"]) check(body,true)^]

fn ast(body) any^ {
 let source="\\ExplSyntaxOn\\def\\v{abc}\\begin{document}" ++ body ++ "\\ExplSyntaxOff\\end{document}";
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;let n=parse(source,{type:"latex"})^;
 {body:body,issue:d.issue,unsupported:d.unsupported,matches:if(d.ast==null)null else d.ast==n}
}
[for (body in ["\\tl_head:N\\v", "\\tl_tail:N\\v", "\\let\\h\\tl_head:N\\h\\v", "\\let\\t\\tl_tail:N\\t\\v",
 "\\exp_args:No\\textbf{\\v}", "\\exp_args:No\\section{\\v}X", "\\exp_args:No\\foo{\\v}",
 "\\def\\v{\\textbf{T}}\\exp_args:No\\use:n{\\v}",
 "\\def\\v{\\section{T}}\\exp_args:No\\use:n{\\v}X", "\\let\\e\\exp_args:No\\e\\textbf{\\v}"]) ast(body)^]

let first=profile.expand_with_kernel("\\ExplSyntaxOn\\def\\v{abc}\\exp_args:No\\use:n{\\v}\\ExplSyntaxOff",kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
let second=expansion.resume(first,"X","<second>",100000)^;
let resumed=digest.digest(second)^;
{first_guard:first.expl3_exp_args_once_seen,first_issue:first.issue,second_guard:second.expl3_exp_args_once_seen,issue:resumed.issue,unsupported:resumed.unsupported}
