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
  "\\ExplSyntaxOn\\int_if_zero:nTF{0}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_odd:nTF{-3}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_even:nTF{4}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_zero:nT{0}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_even:nF{3}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_zero:nTF{1}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_zero:nTF{-1}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_zero:nTF{1-1}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_zero:nT{1}{Y}X\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_zero:nF{0}{N}X\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_zero:nF{4}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_odd:nTF{0}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_odd:nTF{1}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_odd:nTF{2}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_odd:nTF{-2}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_odd:nTF{-2147483647}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_odd:nT{3}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_odd:nT{4}{Y}X\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_odd:nF{4}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_odd:nF{3}{N}X\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_even:nTF{0}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_even:nTF{1}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_even:nTF{-2}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_even:nTF{-3}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_even:nTF{-2147483646}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_even:nT{4}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_even:nT{3}{Y}X\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_even:nF{4}{N}X\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_zero:nTF{\\c_zero_int}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_odd:nTF{\\c_one_int}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_even:nTF{\\int_abs:n{-4}}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_zero:nTF{\\int_eval:n{1-1}}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_even:nTF{\\int_max:nn{2}{4}}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_odd:nTF{\\int_compare:nTF{1<2}{3}{4}}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\n{-7}\\int_if_odd:nTF{\\n}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\count0=8 \\int_if_even:nTF{\\count0}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\edef\\x{\\int_if_zero:nTF{0}{Y}{N}}\\x\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\oddtest\\int_if_odd:nTF\\oddtest{3}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_if_zero:nTF{0}{\\int_if_even:nTF{2}{Y}{N}}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\bad{Z}\\int_if_zero:nTF{0}{Y}{\\bad}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\bad{Z}\\int_if_zero:nTF{1}{\\bad}{N}\\ExplSyntaxOff",
  "\\pdfsetrandomseed 123 \\ExplSyntaxOn\\int_if_odd:nTF{\\pdfuniformdeviate10}{Y}{N},\\pdfuniformdeviate10\\ExplSyntaxOff",
  "\\pdfsetrandomseed 123 \\ExplSyntaxOn\\int_if_zero:nTF{0}{Y}{\\pdfuniformdeviate10},\\pdfuniformdeviate10\\ExplSyntaxOff",
  "\\begin{document}A\\ExplSyntaxOn\\int_if_even:nTF{4}{Y}{N}\\ExplSyntaxOff B\\end{document}"
]) check(source)^]

fn safe_digestion(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 if(d.ast!=null) d.ast==(parse(source,{type:"latex"})^) else d.issue=="latex-digest-unsupported"
}
[for(source in ["\\begin{document}\\ExplSyntaxOn\\int_if_zero:nTF{0}{Y}{N}\\ExplSyntaxOff\\end{document}","\\begin{document}$\\ExplSyntaxOn\\int_if_odd:nT{3}{Y}\\ExplSyntaxOff$\\end{document}","\\ExplSyntaxOn\\edef\\x{\\int_if_even:nTF{4}{Y}{N}}\\ExplSyntaxOff\\begin{document}\\x\\end{document}"]) safe_digestion(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_int_tests_main.tex","text")^;
let resources={local:{'script_int_tests_child.tex':{data:input(root ++ "script_int_tests_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_int_tests_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;
let n=input(root ++ "script_int_tests_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==slice(n.messages,len(baseline),len(n.messages))^,diagnostics_match:r.diagnostic_records==n.diagnostics}
