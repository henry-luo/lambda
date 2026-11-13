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
 "\\ExplSyntaxOn\\bool_if:NTF\\l_tmpa_bool{T}{F}\\bool_if:NF\\l_tmpa_bool{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\bool_set_true:N\\l_tmpa_bool\\bool_if:NTF\\l_tmpa_bool{T}{F}\\bool_if:NT\\l_tmpa_bool{Y}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\bool_set_true:N\\l_tmpa_bool\\bool_set_false:N\\l_tmpa_bool\\bool_if:NTF\\l_tmpa_bool{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn{\\bool_set_true:N\\l_tmpa_bool\\bool_if:NTF\\l_tmpa_bool{T}{F}}\\bool_if:NTF\\l_tmpa_bool{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn{\\bool_gset_true:N\\g_tmpa_bool}\\bool_if:NTF\\g_tmpa_bool{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\bool_gset_true:N\\g_tmpa_bool{\\bool_gset_false:N\\g_tmpa_bool}\\bool_if:NTF\\g_tmpa_bool{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\bool_set_true:c{l_tmpa_bool}\\bool_if:cTF{l_tmpa_bool}{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_eval:n{\\bool_if_p:N\\l_tmpa_bool+2}\\bool_set_true:N\\l_tmpa_bool\\int_eval:n{\\bool_if_p:N\\l_tmpa_bool+2}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\x{\\bool_if:NTF\\l_tmpa_bool{T}{F}}\\x\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\meaning\\bool_if:NTF/\\meaning\\bool_set_true:N\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\let\\a\\bool_if:NTF\\a\\l_tmpa_bool{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\ifx\\l_tmpa_bool\\c_false_bool Y\\else N\\fi\\bool_set_true:N\\l_tmpa_bool\\ifx\\l_tmpa_bool\\c_true_bool Y\\else N\\fi\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\bool_new:N\\l_test_bool\\bool_if:NTF\\l_test_bool{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\bool_new:N\\l_test_bool\\bool_set_true:N\\l_test_bool\\bool_if:NTF\\l_test_bool{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn{\\bool_new:N\\l_test_bool}\\bool_if:NTF\\l_test_bool{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\bool_new:c{l_test_bool}\\bool_if:cTF{l_test_bool}{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\meaning\\bool_new:N\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\def\\b{X}\\cs_new_eq:NN\\a\\b\\ifx\\a\\b Y\\else N\\fi\\a\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\cs_new_eq:NN\\a=\\meaning\\a\\ExplSyntaxOff",
 "\\ExplSyntaxOn{\\globaldefs=-1 \\bool_new:N\\l_test_bool}\\cs_if_exist:NTF\\l_test_bool{Y}{N}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\bool_new:N\\l_test_bool{\\globaldefs=1 \\bool_set_true:N\\l_test_bool}\\bool_if:NTF\\l_test_bool{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\cs_new_eq:NN\\a~\\relax\\ifx\\a\\relax Y\\else N\\fi\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\cs_new_eq:NN\\a\\undefined\\ifx\\a\\undefined Y\\else N\\fi\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\bool_new:N\\l_a_bool\\bool_new:N\\l_b_bool\\bool_set_true:N\\l_b_bool\\bool_set_eq:NN\\l_a_bool\\l_b_bool\\bool_if:NTF\\l_a_bool{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\bool_new:c{l_a_bool}\\bool_set_true:N\\l_tmpa_bool\\bool_set_eq:cN{l_a_bool}\\l_tmpa_bool\\bool_if:cTF{l_a_bool}{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\ifx\\bool_set_eq:NN\\tl_set_eq:NN Y\\else N\\fi\\ifx\\bool_gset_eq:NN\\tl_gset_eq:NN Y\\else N\\fi\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\bool_new:N\\l_a_bool\\bool_set_true:N\\l_tmpa_bool\\bool_set_eq:Nc\\l_a_bool{l_tmpa_bool}\\bool_if:NTF\\l_a_bool{T}{F}\\bool_set_eq:cc{l_a_bool}{l_tmpb_bool}\\bool_if:NF\\l_a_bool{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\bool_new:N\\l_a_bool\\bool_set_true:N\\l_tmpa_bool{\\bool_gset_eq:cc{l_a_bool}{l_tmpa_bool}}\\bool_if:NTF\\l_a_bool{T}{F}\\ExplSyntaxOff"
]) check(source)^]

fn check_ast(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 let n=parse(source,{type:"latex"})^;
 {issue:d.issue,unsupported:d.unsupported,matches:if(d.ast==null)null else d.ast==n,
  actual:if(d.ast==null or d.ast==n)null else d.ast,native:if(d.ast==null or d.ast==n)null else n}
}
[for (source in [
 "\\ExplSyntaxOn\\bool_set_true:N\\l_tmpa_bool\\ExplSyntaxOff\\begin{document}A\\end{document}",
 "\\begin{document}\\ExplSyntaxOn\\bool_if:NTF\\l_tmpa_bool{T}{F}\\ExplSyntaxOff\\end{document}",
 "\\begin{document}\\ExplSyntaxOn\\bool_set_true:N\\l_tmpa_bool\\bool_if:NTF\\l_tmpa_bool{T}{F}\\ExplSyntaxOff\\end{document}",
 "\\ExplSyntaxOn\\bool_if:NTF\\l_tmpa_bool{T}{F}\\ExplSyntaxOff\\begin{document}A\\end{document}", "\\begin{document}X\\ExplSyntaxOn\\bool_if:NTF\\l_tmpa_bool{A}{\\textbf{B}}\\ExplSyntaxOff Y\\end{document}", "\\begin{document}\\ExplSyntaxOn\\bool_if:cTF{l_tmpa_bool}{A}{B}\\ExplSyntaxOff\\end{document}", "\\ExplSyntaxOn\\bool_new:N\\l_test_bool\\bool_set_true:N\\l_test_bool\\ExplSyntaxOff\\begin{document}A\\end{document}"
]) check_ast(source)^]

fn check_constructor(source) any^ { check(source,true) }
[for (source in [
 "\\ExplSyntaxOn\\bool_new:N\\l_test_bool\\begin{document}\\textbf{\\bool_set_true:N\\l_test_bool X}\\bool_if:NTF\\l_test_bool{T}{F}\\ExplSyntaxOff\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textbf{\\bool_if:NTF\\l_tmpa_bool{T}{F}}\\ExplSyntaxOff\\end{document}",
 "\\ExplSyntaxOn\\begin{document}\\textbf{\\bool_set_true:c{l_tmpa_bool} X}\\ExplSyntaxOff\\end{document}"
]) check_constructor(source)^]

let guarded_source="\\ExplSyntaxOn\\begin{document}\\mystery{\\bool_new:N\\l_test_bool X}\\ExplSyntaxOff\\end{document}";
let guarded=profile.expand_with_kernel(guarded_source,kernel,{script_expl3:true,constructor_argument_mode:true,
    echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
{constructor_declaration_guarded:guarded.issue=="tex-unsupported-primitive" and
    guarded.unsupported=="constructor argument expl3 assignment"}
