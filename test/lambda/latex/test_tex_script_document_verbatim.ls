import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let cases=[
 "\\NewDocumentCommand\\foo{v}{[#1]}\\foo{abc}",
 "\\NewDocumentCommand\\foo{v}{[#1]}\\foo{a{b}c}",
 "\\NewDocumentCommand\\foo{v}{[#1]}\\foo{\\unknown X}",
 "\\NewDocumentCommand\\foo{v}{[#1]}\\foo{a#b}",
 "\\NewDocumentCommand\\foo{v m}{#1/#2}\\foo{A}{B}",
 "\\NewDocumentCommand\\foo{v}{#1}\\foo X",
 "\\NewDocumentCommand\\foo{v}{[#1]}\\foo{éclair}",
 "\\NewDocumentCommand\\foo{v}{[#1]}\\foo{}",
 "\\NewDocumentCommand\\foo{v}{[#1]}\\foo{a  b}",
 "\\NewDocumentCommand\\foo{v}{\\detokenize{#1}}\\foo{a#b}",
 "\\NewDocumentCommand\\foo{v}{#1}\\edef\\x{\\foo{A}}\\meaning\\x",
 "\\NewDocumentCommand\\foo{v}{[#1]}\\foo{a~b}"
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
