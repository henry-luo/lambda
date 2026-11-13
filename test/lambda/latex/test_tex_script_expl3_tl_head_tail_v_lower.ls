import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
import expansion: lambda.doc.latex.tex_engine.expansion
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
 "\\tl_set:Nn\\l_tmpa_tl{abc}\\tl_head:v{l_tmpa_tl}/\\tl_tail:v{l_tmpa_tl}",
 "\\tl_set:Nn\\l_tmpa_tl{}\\tl_head:v{l_tmpa_tl}/\\tl_tail:v{l_tmpa_tl}",
 "\\tl_set:Nn\\l_tmpa_tl{{ab}cd}\\tl_head:v{l_tmpa_tl}/\\tl_tail:v{l_tmpa_tl}",
 "\\tl_set:Nn\\l_tmpa_tl{~abc}\\tl_head:v{l_tmpa_tl}/\\tl_tail:v{l_tmpa_tl}",
 "\\tl_set:Nn\\l_tmpa_tl{éclair}\\tl_head:v{l_tmpa_tl}/\\tl_tail:v{l_tmpa_tl}",
 "\\tl_set:Nn\\l_tmpa_tl{abc}\\def\\name{l_tmpa_tl}\\tl_head:v{\\name}/\\tl_tail:v{\\name}",
 "\\tl_set:Nn\\l_tmpa_tl{abc}\\edef\\x{\\tl_head:v{l_tmpa_tl}}\\meaning\\x",
 "\\tl_set:Nn\\l_tmpa_tl{abc}\\edef\\x{\\tl_tail:v{l_tmpa_tl}}\\meaning\\x",
 "\\tl_set:Nn\\l_tmpa_tl{abc}{\\tl_set:Nn\\l_tmpa_tl{xyz}\\tl_head:v{l_tmpa_tl}}\\tl_tail:v{l_tmpa_tl}",
 "\\tl_set:Nn\\l_tmpa_tl{abc}\\let\\h\\tl_head:v\\h{l_tmpa_tl}",
 "\\tl_set:Nn\\l_tmpa_tl{abc}\\let\\h\\tl_tail:v\\h{l_tmpa_tl}",
 "\\tl_set:Nn\\l_tmpa_tl{abc}\\if\\tl_head:v{l_tmpa_tl}a Y\\else N\\fi",
 "\\meaning\\exp_args:Nv/\\meaning\\tl_head:v/\\meaning\\tl_tail:v/\\meaning\\__exp_eval_register:c",
 "\\def\\x{abc}\\exp_args:Nv\\tl_head:n{x}", "\\def\\x{abc}\\exp_args:Nv\\tl_tail:n{x}",
 "\\def\\x{abc}\\exp_args:Nv\\use:n{x}", "\\def\\x{abc}\\def\\name{x}\\exp_args:Nv\\use:n{\\name}",
 "\\def\\x{\\y}\\def\\y{abc}\\tl_head:v{x}/\\tl_tail:v{x}",
 "\\tl_set:Nn\\l_tmpa_tl{\\textbf{X}Y}\\tl_head:v{l_tmpa_tl}",
 "\\tl_set:Nn\\l_tmpa_tl{a\\textbf{X}}\\tl_tail:v{l_tmpa_tl}",
 "\\countdef\\n=0\\relax\\n=123\\relax\\tl_head:v{n}/\\tl_tail:v{n}",
 "\\dimendef\\d=0\\relax\\d=12pt\\relax\\tl_head:v{d}/\\tl_tail:v{d}",
 "\\toksdef\\t=0\\relax\\t={abc}\\relax\\tl_head:v{t}/\\tl_tail:v{t}",
 "\\tl_new:c{l_test_tl}\\tl_set:Nn\\l_test_tl{abc}\\tl_head:v{l_test_tl}/\\tl_tail:v{l_test_tl}"
]) check(body)^];

[for (body in ["\\tl_set:Nn\\l_tmpa_tl{abc}\\begin{document}\\textbf{\\tl_head:v{l_tmpa_tl}}\\end{document}",
 "\\tl_set:Nn\\l_tmpa_tl{abc}\\begin{document}\\section{\\tl_tail:v{l_tmpa_tl}}X\\end{document}"]) check(body,true)^]

fn ast(body) any^ {
 let source="\\ExplSyntaxOn\\def\\v{abc}\\begin{document}" ++ body ++ "\\ExplSyntaxOff\\end{document}";
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;let n=parse(source,{type:"latex"})^;
 {body:body,issue:d.issue,unsupported:d.unsupported,matches:if(d.ast==null)null else d.ast==n}
}
[for (body in ["\\tl_head:v{v}", "\\tl_tail:v{v}", "\\let\\h\\tl_head:v\\h{v}", "\\let\\t\\tl_tail:v\\t{v}",
 "\\exp_args:Nv\\textbf{v}", "\\exp_args:Nv\\section{v}X", "\\let\\e\\exp_args:Nv\\e\\textbf{v}"]) ast(body)^]

let first=profile.expand_with_kernel("\\ExplSyntaxOn\\def\\v{abc}\\exp_args:Nv\\use:n{v}\\ExplSyntaxOff",kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
let second=expansion.resume(first,"X","<second>",100000)^;
let resumed=digest.digest(second)^;
{first_guard:first.expl3_exp_args_value_seen,first_issue:first.issue,second_guard:second.expl3_exp_args_value_seen,issue:resumed.issue,unsupported:resumed.unsupported}
