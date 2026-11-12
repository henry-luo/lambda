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
  "\\ExplSyntaxOn\\int_abs:n{-42}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_abs:n{0}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_abs:n{1-5}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_max:nn{2}{5}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_min:nn{2}{5}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_max:nn{-2}{-5}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_min:nn{-2}{-5}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_max:nn{1+3}{3*2}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_abs:n{\\int_min:nn{-5}{-7}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_min:nn{\\int_abs:n{-9}}{3}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\edef\\x{\\int_abs:n{-6}}\\x\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\count0=8\\int_max:nn{\\count0}{5}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_max:nn{2147483647}{0}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_abs:n{+5}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_abs:n{--5}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_abs:n{-2147483647}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_abs:n{(2-7)*3}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_abs:n{7/2}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_abs:n{-7/2}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_max:nn{0}{0}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_min:nn{0}{0}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_max:nn{100}{-100}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_min:nn{100}{-100}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\n{-17}\\int_abs:n{\\n}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\n{12}\\int_max:nn{\\n}{2*6}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_min:nn{\\int_max:nn{2}{8}}{5}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_max:nn{\\int_min:nn{2}{8}}{5}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_eval:n{\\int_abs:n{-2}+3}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_abs:n{\\int_eval:n{3-5}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\edef\\x{\\int_max:nn{4}{7}}\\x\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\edef\\x{\\int_min:nn{4}{7}}\\x\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\x\\int_abs:n\\x{-4}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\iftrue\\int_abs:n{-6}\\else X\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_max:nn{\\numexpr3+7\\relax}{2}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\count0=-12\\int_abs:n{\\count0}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_max:nn{\\int_min:nn{8}{9}}{\\int_abs:n{-11}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_min:nn{\\int_abs:n{-11}}{\\int_abs:n{-7}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_abs:n{`A-70}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_min:nn{\"10}{15}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_max:nn{\\eTeXversion}{1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_min:nn{2147483647}{-2147483647}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_abs:n{42}A\\int_max:nn{1}{2}B\\ExplSyntaxOff",
  "\\begin{document}A\\ExplSyntaxOn\\int_min:nn{4}{9}\\ExplSyntaxOff B\\end{document}",
  "\\pdfsetrandomseed 123 \\ExplSyntaxOn\\int_abs:n{\\pdfuniformdeviate10},\\pdfuniformdeviate10\\ExplSyntaxOff",
  "\\pdfsetrandomseed 123 \\ExplSyntaxOn\\int_abs:n{-\\pdfuniformdeviate10},\\pdfuniformdeviate10\\ExplSyntaxOff",
  "\\pdfsetrandomseed 123 \\ExplSyntaxOn\\int_max:nn{\\pdfuniformdeviate10}{3},\\pdfuniformdeviate10\\ExplSyntaxOff",
  "\\pdfsetrandomseed 123 \\ExplSyntaxOn\\int_min:nn{\\pdfuniformdeviate10}{3},\\pdfuniformdeviate10\\ExplSyntaxOff",
  "\\pdfsetrandomseed 123 \\ExplSyntaxOn\\int_max:nn{3}{\\pdfuniformdeviate10},\\pdfuniformdeviate10\\ExplSyntaxOff",
  "\\pdfsetrandomseed 123 \\ExplSyntaxOn\\int_min:nn{3}{\\pdfuniformdeviate10},\\pdfuniformdeviate10\\ExplSyntaxOff",
  "\\pdfsetrandomseed 123 \\ExplSyntaxOn\\int_max:nn{\\pdfuniformdeviate10}{\\pdfuniformdeviate10},\\pdfuniformdeviate10\\ExplSyntaxOff",
  "\\pdfsetrandomseed 123 \\ExplSyntaxOn\\int_min:nn{\\pdfuniformdeviate10}{\\pdfuniformdeviate10},\\pdfuniformdeviate10\\ExplSyntaxOff"
]) check(source)^]

fn safe_digestion(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 if(d.ast!=null) d.ast==(parse(source,{type:"latex"})^) else d.issue=="latex-digest-unsupported"
}
[for(source in ["\\begin{document}\\ExplSyntaxOn\\int_abs:n{-4}\\ExplSyntaxOff\\end{document}","\\begin{document}$\\ExplSyntaxOn\\int_min:nn{2}{4}\\ExplSyntaxOff$\\end{document}"]) safe_digestion(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_int_math_main.tex","text")^;
let resources={local:{'script_int_math_child.tex':{data:input(root ++ "script_int_math_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_int_math_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;
let n=input(root ++ "script_int_math_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==slice(n.messages,len(baseline),len(n.messages))^,diagnostics_match:r.diagnostic_records==n.diagnostics}
