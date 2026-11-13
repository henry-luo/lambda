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
 "\\ExplSyntaxOn\\bool_set_inverse:N\\l_tmpa_bool\\bool_if:NTF\\l_tmpa_bool{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\bool_set_inverse:N\\l_tmpa_bool\\bool_set_inverse:N\\l_tmpa_bool\\bool_if:NTF\\l_tmpa_bool{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\bool_set_true:N\\l_tmpa_bool\\bool_set_inverse:N\\l_tmpa_bool\\bool_if:NTF\\l_tmpa_bool{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn{\\bool_set_inverse:N\\l_tmpa_bool\\bool_if:NTF\\l_tmpa_bool{T}{F}}\\bool_if:NTF\\l_tmpa_bool{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn{\\bool_gset_inverse:N\\g_tmpa_bool}\\bool_if:NTF\\g_tmpa_bool{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\bool_set_inverse:c{l_tmpa_bool}\\bool_if:NTF\\l_tmpa_bool{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn{\\bool_gset_inverse:c{g_tmpa_bool}}\\bool_if:cTF{g_tmpa_bool}{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn{\\globaldefs=1\\bool_set_inverse:N\\l_tmpa_bool}\\bool_if:NTF\\l_tmpa_bool{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn{\\globaldefs=-1\\bool_gset_inverse:N\\l_tmpa_bool}\\bool_if:NTF\\l_tmpa_bool{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\meaning\\bool_set_inverse:N/\\meaning\\bool_gset_inverse:N/\\meaning\\bool_set_inverse:c\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\bool_new:N\\l_foo_bool\\bool_set_inverse:N\\l_foo_bool\\bool_if:NTF\\l_foo_bool{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\x{\\bool_set_inverse:N\\l_tmpa_bool}\\meaning\\x\\ExplSyntaxOff"
]) check(source)^];
[check("\\ExplSyntaxOn\\begin{document}\\textbf{\\bool_set_inverse:N\\l_tmpa_bool X}\\bool_if:NTF\\l_tmpa_bool{T}{F}\\ExplSyntaxOff\\end{document}", true)^]

fn check_ast(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^; let n=parse(source,{type:"latex"})^;
 {issue:d.issue,unsupported:d.unsupported,matches:if(d.ast==null)null else d.ast==n,
  actual:if(d.ast==null or d.ast==n)null else d.ast,native:if(d.ast==null or d.ast==n)null else n}
}
[for (source in [
 "\\ExplSyntaxOn\\bool_set_inverse:N\\l_tmpa_bool\\ExplSyntaxOff\\begin{document}A\\end{document}",
 "\\begin{document}\\ExplSyntaxOn\\bool_set_inverse:c{l_tmpa_bool}\\bool_if:NTF\\l_tmpa_bool{T}{F}\\ExplSyntaxOff\\end{document}"
]) check_ast(source)^]
