import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let cases=[
 "\\NewDocumentCommand\\foo{e{^_}}{[#1]}\\foo^A_B",
 "\\NewDocumentCommand\\foo{E{^_}{{A}{B}}}{[#1]}\\foo^X",
 "\\NewDocumentCommand\\foo{e{^_}m}{#1/#2}\\foo{A}",
 "\\NewDocumentCommand\\foo{me{^}}{#1/#2}\\foo{A}^B",
 "\\NewDocumentCommand\\foo{e{^}e{_}}{#1/#2}\\foo",
 "\\NewDocumentCommand\\foo{e{^}}{[#1]}\\foo/\\foo^A",
 "\\NewDocumentEnvironment{foo}{e{^}}{[#1]}{!}\\begin{foo}X\\end{foo}",
 "\\NewDocumentCommand\\foo{e{^}}{A}\\NewDocumentCommand\\foo{e{_}}{B}\\foo",
 "\\RenewDocumentCommand\\foo{e{^}}{A}\\foo",
 "\\NewDocumentCommand\\foo{+e{^}}{[#1]}\\foo^X"
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
