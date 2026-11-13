import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let baseline=parse("",{type:"tex"})^.messages;
fn check(source, constructor = false) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,constructor_argument_mode:constructor,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let n=parse(source,{type:"tex"})^;
 let b=reconstruct.reconstruct(r)^;
 {issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==slice(n.messages,len(baseline),len(n.messages))^,diagnostics_match:r.diagnostic_records==n.diagnostics}
}
[for (source in [
 "\\ExplSyntaxOn\\int_eval:n{\\int_if_zero_p:n{0}+2}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_eval:n{\\int_if_odd_p:n{-3}+2}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_eval:n{\\int_if_even_p:n{8}+2}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\meaning\\int_if_zero_p:n/\\meaning\\int_if_odd_p:n/\\meaning\\int_if_even_p:n\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_eval:n{\\int_if_zero_p:n{1}+\\int_if_zero_p:n{0}}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_eval:n{\\int_if_odd_p:n{0}+\\int_if_odd_p:n{3}}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_eval:n{\\int_if_even_p:n{-4}+\\int_if_even_p:n{-3}}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_eval:n{\\int_if_zero_p:n{2-2}+\\int_if_odd_p:n{2*3+1}}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_eval:n{\\int_if_even_p:n{\\int_eval:n{2+4}}}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\x{\\int_if_odd_p:n{3}}\\meaning\\x\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\ifnum\\int_if_zero_p:n{0}=1 Y\\else N\\fi\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\ifnum\\int_if_odd_p:n{8}=1 Y\\else N\\fi\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\let\\p\\int_if_even_p:n\\int_eval:n{\\p{4}}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\cs_if_exist:NTF\\int_if_zero_p:n{Y}{N}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_eval:n{\\int_if_zero_p:n{0}+\\int_if_odd_p:n{-5}+\\int_if_even_p:n{10}}\\ExplSyntaxOff"
]) check(source)^]

fn check_ast(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^; let n=parse(source,{type:"latex"})^;
 {issue:d.issue,unsupported:d.unsupported,matches:if(d.ast==null)null else d.ast==n,
  actual:if(d.ast==null or d.ast==n)null else d.ast,native:if(d.ast==null or d.ast==n)null else n}
}
[for (source in [
 "\\ExplSyntaxOn\\begin{document}\\int_if_zero_p:n{0}\\ExplSyntaxOff\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\int_eval:n{\\int_if_odd_p:n{3}}\\ExplSyntaxOff\\end{document}",
 "\\begin{document}\\ExplSyntaxOn\\ifnum\\int_if_even_p:n{2}=1 T\\else F\\fi\\ExplSyntaxOff\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\int_if_zero_p:n{4}\\ExplSyntaxOff\\end{document}",
 "\\ExplSyntaxOn\\begin{document}A\\int_if_odd_p:n{-5}B\\ExplSyntaxOff\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textbf{\\int_if_even_p:n{4}}\\ExplSyntaxOff\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\section{\\int_if_zero_p:n{0}}X\\ExplSyntaxOff\\end{document}",
 "\\ExplSyntaxOn\\edef\\x{\\int_if_odd_p:n{3}}\\begin{document}\\x\\ExplSyntaxOff\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\int_if_even_p:n{0}\\int_if_odd_p:n{0}\\ExplSyntaxOff\\end{document}"
]) check_ast(source)^]

check("\\ExplSyntaxOn\\begin{document}\\textbf{\\int_eval:n{\\int_if_zero_p:n{0}}}\\ExplSyntaxOff\\end{document}",true)^;

[for (source in [
 "\\ExplSyntaxOn\\begin{document}\\textbf{\\int_if_even_p:n{4}}\\ExplSyntaxOff\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textbf{\\int_if_zero_p:n{1}}\\ExplSyntaxOff\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\section{\\int_if_odd_p:n{-3}}X\\ExplSyntaxOff\\end{document}"
]) check(source,true)^]

check_ast("\\chardef\\x=65\\begin{document}\\textbf{\\x}\\end{document}")^
