import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let style=input(sys.lambda.home ++ "/package/doc/latex/tex/ifthen.sty","text")^;
// a bundled file is loaded with and without a corresponding renderer adapter.
let cases=["\\usepackage{ifthen}\\begin{document}\\IfPackageLoadedTF{ifthen}{YES}{NO}\\end{document}","\\usepackage{ifthen}\\usepackage{ifthen}\\begin{document}\\IfPackageLoadedTF{ifthen}{YES}{NO}\\end{document}"];
fn check(s,adapters) any^ {
 let r=profile.expand_with_kernel(s,kernel,{echo_definitions:true,constructor_argument_mode:true,adapters:adapters,resources:{local:{},bundled:{'ifthen.sty':{data:style}}}},100000)^;
 let n=parse(s,{type:"tex",packages:adapters})^;
 {issue:r.issue,text:if(reconstruct.reconstruct(r)^.text==n.text)true else [reconstruct.reconstruct(r)^.text,n.text],diagnostics:if(r.diagnostic_records==n.diagnostics)true else [r.diagnostic_records,n.diagnostics]}
}
[for(s in cases,a in [[],["ifthen"]])check(s,a)^]
