import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let cases=[
 "\\NewDocumentCommand\\foo{>{\\TrimSpaces}m}{[#1]}\\foo{ A }",
 "\\NewDocumentCommand\\foo{> {\\TrimSpaces} m}{[#1]}\\foo{ A }",
 "\\NewDocumentCommand\\foo{>{\\TrimSpaces}>{\\ReverseBoolean}m}{[#1]}\\foo{A}",
 "\\NewDocumentCommand\\foo{>{X}sm}{#1/#2}\\foo*{A}",
 "\\NewDocumentCommand\\foo{e{^}>{X}m}{#1/#2}\\foo{A}",
 "\\NewDocumentCommand\\foo{>{X}e{^}}{[#1]}\\foo^A",
 "\\NewDocumentCommand\\foo{z}{A}\\foo{X}",
 "\\NewDocumentCommand\\foo{m z}{A}\\foo{X}",
 "\\NewDocumentCommand\\foo{+ m}{A}\\foo{X}",
 "\\NewDocumentCommand\\foo{mmmmmmmmmm}{A}\\foo{X}",
 "\\NewDocumentCommand\\foo{m}{A}\\NewDocumentCommand\\foo{z}{B}\\foo{X}",
 "\\NewDocumentEnvironment{foo}{>{X}m}{[#1]}{!}\\begin{foo}{A}X\\end{foo}",
 "\\NewDocumentCommand\\foo{t}{[#1]}\\foo X",
 "\\NewDocumentCommand\\foo{d}{[#1]}\\foo X",
 "\\NewDocumentCommand\\foo{D}{[#1]}\\foo X",
 "\\NewDocumentCommand\\foo{r(}{[#1]}\\foo X",
 "\\NewDocumentCommand\\foo{D(}{[#1]}\\foo X",
 "\\NewDocumentCommand\\foo{+}{[#1]}\\foo X",
 "\\NewDocumentCommand\\foo{>}{[#1]}\\foo X",
 "\\NewDocumentCommand\\foo{é}{A}\\foo X",
 "\\NewDocumentCommand\\foo{λ}{A}\\foo X",
 "\\NewDocumentCommand\\foo{ā}{A}\\foo X",
 "\\NewDocumentCommand\\foo{Ā}{A}\\foo X"
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
