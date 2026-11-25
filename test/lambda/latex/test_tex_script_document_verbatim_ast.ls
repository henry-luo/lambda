import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let cases=[
 "\\NewDocumentCommand\\foo{v}{[#1]}\\begin{document}\\foo{abc}\\end{document}",
 "\\NewDocumentCommand\\foo{v}{[#1]}\\begin{document}\\foo{a{b}c}\\end{document}",
 "\\NewDocumentCommand\\foo{v}{[#1]}\\begin{document}\\foo{\\unknown X}\\end{document}",
 "\\NewDocumentCommand\\foo{v}{[#1]}\\begin{document}\\textbf{\\foo{a#b}}\\end{document}"
];
fn check(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{constructor_argument_mode:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let n=parse(source,{type:"tex"})^; let d=digest.digest(r)^;let tree=parse(source,{type:"latex"})^;
 let text=reconstruct.reconstruct(r)^.text;
 {source:source,issue:d.issue,unsupported:d.unsupported,text_match:text==n.text,diagnostics_match:r.diagnostic_records==n.diagnostics,
 ast_match:if(d.ast==null)null else d.ast==tree,actual:if(d.ast==null or d.ast==tree)null else d.ast,
 native:if(d.ast==null or d.ast==tree)null else tree}
}
let checked=[for(s in cases) check(s)^];
{cases:len(checked),failures:[for(item in checked where item.issue!=null or not item.text_match or
 not item.diagnostics_match or not item.ast_match) item]}
