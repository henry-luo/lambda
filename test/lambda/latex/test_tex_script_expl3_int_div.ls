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
 "\\ExplSyntaxOn\\int_div_truncate:nn{7}{3},\\int_div_truncate:nn{-7}{3},\\int_div_truncate:nn{7}{-3},\\int_div_truncate:nn{-7}{-3}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_div_round:nn{7}{2},\\int_div_round:nn{-7}{2},\\int_div_round:nn{7}{-2},\\int_div_round:nn{-7}{-2}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_mod:nn{7}{3},\\int_mod:nn{-7}{3},\\int_mod:nn{7}{-3},\\int_mod:nn{-7}{-3}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_div_truncate:nn{0}{3},\\int_div_truncate:nn{1}{3},\\int_div_truncate:nn{-1}{3},\\int_div_truncate:nn{6}{3}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_div_round:nn{1}{2},\\int_div_round:nn{-1}{2},\\int_div_round:nn{1}{3},\\int_div_round:nn{-1}{3}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_mod:nn{0}{7},\\int_mod:nn{1}{7},\\int_mod:nn{-1}{7},\\int_mod:nn{6}{3}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_div_truncate:nn{2147483647}{1},\\int_mod:nn{-2147483647}{3}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_div_round:nn{2147483647}{2},\\int_div_round:nn{-2147483647}{2}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_div_truncate:nn{1+9}{1+2},\\int_mod:nn{2*7}{2+3}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_eval:n{\\int_mod:nn{-7}{3}+\\int_div_round:nn{5}{2}}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\x{\\int_div_truncate:nn{-7}{3}}\\x\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\let\\x\\int_mod:nn\\x{-7}{3}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\count0=-12\\int_mod:nn{\\count0}{5}\\ExplSyntaxOff",
 "\\pdfsetrandomseed 123 \\ExplSyntaxOn\\int_mod:nn{\\pdfuniformdeviate10}{3},\\pdfuniformdeviate10\\ExplSyntaxOff",
 "\\pdfsetrandomseed 123 \\ExplSyntaxOn\\int_div_truncate:nn{7}{\\pdfuniformdeviate10+1},\\pdfuniformdeviate10\\ExplSyntaxOff",
 "\\begin{document}A\\ExplSyntaxOn\\int_mod:nn{-7}{3}\\ExplSyntaxOff B\\end{document}"
,

 "\\ExplSyntaxOn\\int_div_truncate:nn{2147483647}{2147483647},\\int_mod:nn{2147483647}{2147483647}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_div_truncate:nn{-2147483647}{2147483646},\\int_mod:nn{-2147483647}{2147483646}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_div_truncate:nn{2147483647}{-2147483646},\\int_mod:nn{2147483647}{-2147483646}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_div_round:nn{2147483646}{2147483647},\\int_div_round:nn{-2147483646}{2147483647}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_div_truncate:nn{2}{-2147483647},\\int_mod:nn{-2}{-2147483647}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_div_round:nn{1073741823}{2147483646},\\int_div_round:nn{-1073741823}{2147483646}\\ExplSyntaxOff",
 "\\pdfsetrandomseed 123 \\ExplSyntaxOn\\int_mod:nn{\\pdfuniformdeviate10}{\\pdfuniformdeviate10+1},\\pdfuniformdeviate10\\ExplSyntaxOff"
]) check(source)^]


fn safe_digestion(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 if(d.ast!=null) d.ast==(parse(source,{type:"latex"})^) else d.issue=="latex-digest-unsupported"
}
[for (source in ["\\begin{document}\\ExplSyntaxOn\\int_mod:nn{-7}{3}\\ExplSyntaxOff\\end{document}",
    "\\begin{document}$\\ExplSyntaxOn\\int_div_round:nn{7}{2}\\ExplSyntaxOff$\\end{document}"]) safe_digestion(source)^]
