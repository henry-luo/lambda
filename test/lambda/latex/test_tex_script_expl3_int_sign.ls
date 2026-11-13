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
 "\\ExplSyntaxOn\\int_sign:n{-42},\\int_sign:n{0},\\int_sign:n{42}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_sign:n{1-5},\\int_sign:n{(2-7)*3},\\int_sign:n{3-3}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_sign:n{-7/2},\\int_sign:n{7/2}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_sign:n{+5},\\int_sign:n{--5},\\int_sign:n{-2147483647}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\def\\n{-17}\\int_sign:n{\\n}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_sign:n{\\int_eval:n{3-5}}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_eval:n{4+\\int_sign:n{-2}}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_abs:n{\\int_sign:n{-5}}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\x{\\int_sign:n{-6}}\\x\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\let\\x\\int_sign:n\\x{-4}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\count0=-12\\int_sign:n{\\count0}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\iftrue\\int_sign:n{-6}\\else X\\fi\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_sign:n{`A-70}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\int_sign:n{\\numexpr3+7\\relax}\\ExplSyntaxOff",
 "\\pdfsetrandomseed 123 \\ExplSyntaxOn\\int_sign:n{\\pdfuniformdeviate10},\\pdfuniformdeviate10\\ExplSyntaxOff",
 "\\pdfsetrandomseed 123 \\ExplSyntaxOn\\int_sign:n{-\\pdfuniformdeviate10},\\pdfuniformdeviate10\\ExplSyntaxOff",
 "\\begin{document}A\\ExplSyntaxOn\\int_sign:n{-7}\\ExplSyntaxOff B\\end{document}"
]) check(source)^]

fn safe_digestion(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 if(d.ast!=null) d.ast==(parse(source,{type:"latex"})^) else d.issue=="latex-digest-unsupported"
}
[for (source in ["\\begin{document}\\ExplSyntaxOn\\int_sign:n{-4}\\ExplSyntaxOff\\end{document}",
    "\\begin{document}$\\ExplSyntaxOn\\int_sign:n{2}\\ExplSyntaxOff$\\end{document}"]) safe_digestion(source)^]
