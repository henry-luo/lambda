import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let cases=[
 "\\NewDocumentCommand\\foo{o}{\\IfNoValueTF{#1}{N}{Y:#1}}\\foo/\\foo[A]",
 "\\NewDocumentCommand\\foo{g}{\\IfValueTF{#1}{Y:#1}{N}}\\foo/\\foo{A}",
 "\\NewDocumentCommand\\foo{G{Z}m}{#1/#2}\\foo A/\\foo{X}{Y}",
 "\\NewDocumentCommand\\foo{so}{\\IfBooleanTF{#1}{Y}{N}\\IfValueTF{#2}{#2}{-}}\\foo*[A]/\\foo",
 "\\IfNoValueTF{-NoValue-}{T}{F}/\\IfValueF{-NoValue-}{Y}",
 "\\IfNoValueT{-NoValue-}{N}/\\IfNoValueF{X}{Y}/\\IfValueT{X}{V}",
 "\\IfNoValueTF{}{N}{Y}/\\IfValueTF{}{Y}{N}",
 "\\NewDocumentCommand\\foo{o}{\\IfNoValueTF{#1}{N}{[\\detokenize{#1}]}}\\foo[]/\\foo",
 "\\NewDocumentCommand\\foo{g}{\\IfValueTF{#1}{Y}{N}}\\foo{}/\\foo X",
 "\\NewDocumentCommand\\foo{G{}m}{[#1/#2]}\\foo A/\\foo{Z}{B}",
 "\\NewDocumentCommand\\foo{o m}{#1/#2}\\foo{A}/\\foo[X]{B}",
 "\\def\\n{-NoValue-}\\IfNoValueTF{\\n}{N}{Y}/\\IfValueTF{\\n}{Y}{N}",
 "\\NewDocumentCommand\\foo{o}{#1}\\meaning\\foo/\\foo[A]"
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
