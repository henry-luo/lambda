import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import expansion: lambda.doc.latex.tex_engine.expansion
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let baseline=parse("",{type:"tex"})^.messages;
fn check(source, constructor = false) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,constructor_argument_mode:constructor,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let n=parse(source,{type:"tex"})^;let b=reconstruct.reconstruct(r)^;
 {source:source,issue:r.issue,unsupported:r.unsupported,reconstruct_issue:b.issue,text_match:if(r.issue==null)b.text==n.text else null,
 messages_match:r.messages==slice(n.messages,len(baseline),len(n.messages))^,diagnostics_match:r.diagnostic_records==n.diagnostics}
}
[for (body in [
 "\\tl_tail:n{abc}", "\\tl_tail:n{a}", "\\tl_tail:n{}", "\\tl_tail:n{{ab}c}",
 "\\tl_tail:n{{{ab}}c}", "\\tl_tail:n{{}c}", "\\tl_tail:n{~abc}", "\\tl_tail:n{~~abc}",
 "\\tl_tail:n{a~bc}", "\\tl_tail:n{~~}", "\\tl_tail:n{~{ab}c}",
 "\\tl_tail:n{éclair}", "\\tl_tail:n{#abc}", "\\tl_tail:n{$abc}",
 "\\meaning\\tl_tail:n", "\\meaning\\__kernel_tl_to_str:w", "\\meaning\\tex_expanded:D",
 "\\edef\\x{\\tl_tail:n{abc}}\\meaning\\x", "\\edef\\x{\\tl_tail:n{{ab}c}}\\meaning\\x",
 "\\def\\x{XY}\\tl_tail:n{\\x Z}", "\\def\\x{XY}\\edef\\y{\\tl_tail:n{\\x Z}}\\meaning\\y",
 "\\def\\x{AB}\\tl_tail:n{{\\x}Z}", "\\let\\h\\tl_tail:n\\h{abc}",
 "\\ifx\\tl_tail:n\\tl_tail:n Y\\else N\\fi", "\\tl_tail:n{\\textbf{A}B}",
 "\\tl_tail:n{\\c_zero_int A}", "\\tl_tail:n{\\tl_tail:n{ab}}",
 "\\ifcat\\tl_tail:n{a}a Y\\else N\\fi", "\\ifcat\\tl_tail:n{1}a Y\\else N\\fi",
 "\\if\\tl_tail:n{a}a Y\\else N\\fi", "\\if\\tl_tail:n{b}a Y\\else N\\fi",
 "\\def\\h{\\tl_tail:n{abc}}\\edef\\x{\\h}\\meaning\\x",
 "\\let\\h=\\tl_tail:n\\meaning\\h"
]) check("\\ExplSyntaxOn" ++ body ++ "\\ExplSyntaxOff")^]

fn check_ast(body) any^ {
 let source="\\ExplSyntaxOn\\begin{document}" ++ body ++ "\\ExplSyntaxOff\\end{document}";
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;let n=parse(source,{type:"latex"})^;
 {body:body,issue:d.issue,unsupported:d.unsupported,matches:if(d.ast==null)null else d.ast==n,actual:if(d.ast==null or d.ast==n)null else d.ast,native:if(d.ast==null or d.ast==n)null else n}
}
[for (body in ["\\tl_tail:n{abc}","A\\tl_tail:n{{ab}c}B", "\\tl_tail:n{}X", "\\tl_tail:n{~abc}",
 "\\tl_tail:n{\\textbf{A}B}","\\textbf{\\tl_tail:n{abc}}", "\\section{\\tl_tail:n{abc}}X",
 "\\let\\h=\\tl_tail:n\\h{abc}", "\\tl_tail:n{a\\textbf{X}}", "\\tl_tail:n{{a}\\textbf{X}}",
 "\\tl_tail:n{a\\section{T}}Z", "\\tl_tail:n{a\\foo{X}}"]) check_ast(body)^];

[for (source in ["\\ExplSyntaxOn\\begin{document}\\textbf{\\tl_tail:n{abc}}\\ExplSyntaxOff\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\section{\\tl_tail:n{{ab}c}}X\\ExplSyntaxOff\\end{document}"]) check(source,true)^]

let first=profile.expand_with_kernel("\\ExplSyntaxOn\\tl_tail:n{abc}\\ExplSyntaxOff",kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
let second=expansion.resume(first,"X","<second>",100000)^;
let d=digest.digest(second)^;
{first_guard:first.expl3_tl_tail_seen,first_issue:first.issue,groups:len(first.groups),conditions:len(first.conditions),second_guard:second.expl3_tl_tail_seen,issue:d.issue,unsupported:d.unsupported}
