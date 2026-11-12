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
  "\\ExplSyntaxOn\\int_compare:nNnTF{2}<{3}{yes}{no}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnTF{3}<{2}{yes}{no}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnTF{3}={3}{yes}{no}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnTF{-3}>{-7}{yes}{no}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnT{2}<{3}{yes}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnT{2}>{3}{yes}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnF{2}<{3}{no}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnF{2}>{3}{no}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnTF{2+3}={9-4}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnTF{\\int_abs:n{-5}}>{4}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\edef\\x{\\int_compare:nNnTF{2}<{3}{Y}{N}}\\x\\ExplSyntaxOff",
  "\\pdfsetrandomseed 123 \\ExplSyntaxOn\\int_compare:nNnTF{\\pdfuniformdeviate10}<{3}{Y}{N},\\pdfuniformdeviate10\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnTF{0}={0}{same}{diff}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnTF{0}<{0}{yes}{no}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnTF{0}>{0}{yes}{no}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnTF{5}={4}{yes}{no}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnT{5}={5}{T}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnF{5}={4}{F}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnTF{-2147483647}<{2147483647}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnTF{(2+3)*4}>{19}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnTF{7/2}={4}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnTF{-7/2}={-4}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\n{6}\\int_compare:nNnTF{\\n}>{3}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\count0=5 \\int_compare:nNnTF{\\count0}={5}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnTF{\\int_eval:n{2+3}}={5}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnTF{\\int_max:nn{2}{3}}>{2}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnTF{1}<{2}{\\int_compare:nNnTF{3}>{2}{A}{B}}{C}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\bad{Z}\\int_compare:nNnTF{1}<{2}{A}{\\bad}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\bad{Z}\\int_compare:nNnTF{2}<{1}{\\bad}{B}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnTF{2}<{3}{}{N}X\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnTF{3}<{2}{Y}{}X\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\cmp\\int_compare:nNnTF\\cmp{1}<{2}{Y}{N}\\ExplSyntaxOff",
  "\\pdfsetrandomseed 123 \\ExplSyntaxOn\\int_compare:nNnTF{\\pdfuniformdeviate10}<{\\pdfuniformdeviate10}{Y}{N},\\pdfuniformdeviate10\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\iftrue\\int_compare:nNnTF{2}<{3}{Y}{N}\\else X\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnTF{`A}>{64}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnTF{\"10}={16}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nNnTF{\\eTeXversion}>{1}{Y}{N}\\ExplSyntaxOff",
  "\\begin{document}A\\ExplSyntaxOn\\int_compare:nNnTF{1}<{2}{Y}{N}\\ExplSyntaxOff B\\end{document}"
]) check(source)^]

fn safe_digestion(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 if(d.ast!=null) d.ast==(parse(source,{type:"latex"})^) else d.issue=="latex-digest-unsupported"
}
[for(source in ["\\begin{document}\\ExplSyntaxOn\\int_compare:nNnTF{1}<{2}{Y}{N}\\ExplSyntaxOff\\end{document}","\\begin{document}$\\ExplSyntaxOn\\int_compare:nNnT{2}>{1}{Y}\\ExplSyntaxOff$\\end{document}"]) safe_digestion(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_int_compare_main.tex","text")^;
let resources={local:{'script_int_compare_child.tex':{data:input(root ++ "script_int_compare_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_int_compare_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;
let n=input(root ++ "script_int_compare_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==slice(n.messages,len(baseline),len(n.messages))^,diagnostics_match:r.diagnostic_records==n.diagnostics}
