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
 "\\ExplSyntaxOn\\bool_to_str:N\\l_tmpa_bool\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\bool_set_true:N\\l_tmpa_bool\\bool_to_str:N\\l_tmpa_bool\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\bool_to_str:c{l_tmpa_bool}\\bool_set_true:c{l_tmpa_bool}\\bool_to_str:c{l_tmpa_bool}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\x{\\bool_to_str:N\\l_tmpa_bool}\\edef\\y{\\tl_to_str:n{false}}\\ifx\\x\\y Y\\else N\\fi\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\bool_set_true:N\\l_tmpa_bool\\edef\\x{\\bool_to_str:N\\l_tmpa_bool}\\edef\\y{\\tl_to_str:n{true}}\\ifx\\x\\y Y\\else N\\fi\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\meaning\\bool_to_str:N/\\meaning\\bool_to_str:c\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\bool_set_inverse:N\\l_tmpa_bool\\bool_to_str:c{l_tmpa_bool}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\x{\\bool_to_str:N\\l_tmpa_bool}\\meaning\\x\\ExplSyntaxOff"
]) check(source)^]

fn check_ast(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^; let n=parse(source,{type:"latex"})^;
 {issue:d.issue,unsupported:d.unsupported,matches:if(d.ast==null)null else d.ast==n,
  actual:if(d.ast==null or d.ast==n)null else d.ast,native:if(d.ast==null or d.ast==n)null else n}
}
[for (source in [
 "\\begin{document}\\ExplSyntaxOn\\bool_to_str:N\\l_tmpa_bool\\ExplSyntaxOff\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\bool_to_str:c{l_tmpa_bool}\\ExplSyntaxOff\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textbf{\\bool_to_str:N\\l_tmpa_bool}\\ExplSyntaxOff\\end{document}"
]) check_ast(source)^]

check("\\ExplSyntaxOn\\begin{document}\\textbf{\\bool_to_str:c{l_tmpa_bool}}\\ExplSyntaxOff\\end{document}", true)^
