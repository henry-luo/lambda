import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let baseline=parse("",{type:"tex"})^.messages;
fn check(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let n=parse(source,{type:"tex"})^;
 let b=reconstruct.reconstruct(r)^;
 {issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==slice(n.messages,len(baseline),len(n.messages))^,diagnostics_match:r.diagnostic_records==n.diagnostics}
}
[for (source in [
  "\\ExplSyntaxOn\\number\\c_zero_int,\\number\\c_one_int\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\c_zero_int,\\meaning\\c_one_int\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_eval:n{\\c_zero_int+\\c_one_int}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_abs:n{-\\c_one_int}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_max:nn{\\c_zero_int}{\\c_one_int}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_min:nn{\\c_zero_int}{\\c_one_int}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnTF{\\c_zero_int}<{\\c_one_int}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\c_zero_int\\c_one_int Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\c_zero_int\\c_zero_int Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifdefined\\c_zero_int Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifdefined\\c_one_int Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\z\\c_zero_int\\number\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\o\\c_one_int\\number\\o\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\edef\\x{\\number\\c_one_int}\\x\\ExplSyntaxOff",
  "\\begin{document}\\ExplSyntaxOn\\int_eval:n{\\c_zero_int+\\c_one_int}\\ExplSyntaxOff\\end{document}",
  "\\ExplSyntaxOn\\int_eval:n{\\c_one_int*5+\\c_zero_int}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnTF{\\c_one_int}={1}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnF{\\c_one_int}<{\\c_zero_int}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_max:nn{\\c_one_int}{-1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_min:nn{\\c_zero_int}{1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\x{\\c_one_int}\\number\\x\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\edef\\x{\\int_eval:n{\\c_one_int+1}}\\x\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifnum\\c_zero_int<\\c_one_int Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifodd\\c_one_int Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn{\\def\\c_one_int{7}\\number\\c_one_int}\\number\\c_one_int\\ExplSyntaxOff",
  "\\ExplSyntaxOn{\\let\\c_zero_int\\c_one_int\\number\\c_zero_int}\\number\\c_zero_int\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\c_one_int\\c_one_int Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_abs:n{\\c_zero_int-\\c_one_int}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\count0=\\c_one_int \\number\\count0\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\count0=\\c_zero_int \\number\\count0\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\number\\numexpr\\c_one_int+\\c_one_int\\relax\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\c_one_int\\ExplSyntaxOff"
]) check(source)^]

fn safe_digestion(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 if(d.ast!=null) d.ast==(parse(source,{type:"latex"})^) else d.issue=="latex-digest-unsupported"
}
[for(source in ["\\begin{document}\\ExplSyntaxOn\\number\\c_zero_int\\ExplSyntaxOff\\end{document}","\\begin{document}\\ExplSyntaxOn\\meaning\\c_one_int\\ExplSyntaxOff\\end{document}","\\begin{document}\\ExplSyntaxOn A\\c_zero_int B\\ExplSyntaxOff\\end{document}","\\begin{document}\\ExplSyntaxOn A\\c_one_int B\\ExplSyntaxOff\\end{document}","\\mathchardef\\foo=1 \\begin{document}A\\foo B\\end{document}"]) safe_digestion(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_int_constants_main.tex","text")^;
let resources={local:{'script_int_constants_child.tex':{data:input(root ++ "script_int_constants_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_int_constants_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;
let n=input(root ++ "script_int_constants_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==slice(n.messages,len(baseline),len(n.messages))^,diagnostics_match:r.diagnostic_records==n.diagnostics}
