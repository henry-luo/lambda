import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let cases=[
 "\\NewDocumentCommand\\foo{+m}{[#1]}\\foo{A}",
 "\\NewDocumentCommand\\foo{!o}{[#1]}\\foo[A]/\\foo",
 "\\NewDocumentCommand\\foo{+!m}{[#1]}\\foo{A}",
 "\\NewDocumentCommand\\foo{+m}{[#1]}\\foo{A\n\nB}",
 "\\NewDocumentCommand\\foo{!O{Z}m}{#1/#2}\\foo{A}/\\foo[X]{B}",
 "\\NewDocumentCommand\\foo{+s+m}{#1/#2}\\foo*{A}/\\foo{B}",
 "\\NewDocumentCommand\\foo{!d()}{[#1]}\\foo(A)/\\foo",
 "\\NewDocumentEnvironment{foo}{+b}{[#1]}{!}\\begin{foo}A\n\nB\\end{foo}",
 "\\NewDocumentCommand\\foo{m !g}{#1/#2}\\foo{A}{B}/\\foo{C}",
 "\\NewDocumentCommand\\foo{!v}{[#1]}\\foo{A{B}}"
];
fn check(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let n=parse(source,{type:"tex"})^;
 let text=reconstruct.reconstruct(r)^.text;
 {source:source,issue:r.issue,unsupported:r.unsupported,text_match:text==n.text,actual:if(text==n.text)null else text,
 native:if(text==n.text)null else n.text,diagnostics_match:r.diagnostic_records==n.diagnostics,
 actual_diagnostics:r.diagnostic_records,native_diagnostics:n.diagnostics}
}
let checked=[for(s in cases) check(s)^];
{cases:len(cases),failures:[for(x in checked where x.issue!=null or not x.text_match or not x.diagnostics_match) x]}
