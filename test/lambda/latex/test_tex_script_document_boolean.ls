import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let cases=[
 "\\NewDocumentCommand\\foo{s}{[#1]}\\foo*/\\foo",
 "\\NewDocumentCommand\\foo{sm}{\\IfBooleanTF{#1}{Y}{N}:#2}\\foo*{A}/\\foo{B}",
 "\\NewDocumentCommand\\foo{ms}{#1\\IfBooleanT{#2}{!}}\\foo{A}*/\\foo{B}",
 "\\NewDocumentCommand\\foo{t+ m}{\\IfBooleanTF{#1}{Y}{N}#2}\\foo+{A}/\\foo{B}",
 "\\NewDocumentCommand\\foo{sO{Z}m}{#1/#2/#3}\\foo*[X]{A}/\\foo{B}",
 "\\IfBooleanTF{\\BooleanTrue}{T}{F}/\\IfBooleanF{\\BooleanFalse}{N}",
 "\\IfBooleanTF{\\BooleanFalse}{T}{F}/\\IfBooleanT{\\BooleanFalse}{X}",
 "\\IfBooleanTF{true}{T}{F}/\\IfBooleanTF{\\BooleanTrue X}{T}{F}",
 "\\NewDocumentCommand\\foo{s}{#1}\\foo   */\\foo X",
 "\\NewDocumentCommand\\foo{ss}{#1/#2}\\foo**/\\foo*/\\foo",
 "\\NewDocumentCommand\\foo{t\\relax}{#1}\\foo\\relax/\\foo X",
 "\\NewDocumentCommand\\foo{s}{#1}\\let\\bar\\foo\\bar*/\\bar",
 "\\NewDocumentCommand\\foo{s}{#1}\\edef\\saved{\\foo*}\\meaning\\saved",
 "\\NewDocumentCommand\\foo{m}{[#1]}\\foo{A}",
 "\\NewDocumentCommand\\foo{O{Z}}{[#1]}\\foo/\\foo[X]",
 "\\NewDocumentCommand\\foo{O{D}m}{#1/#2}\\foo{X}/\\foo[Q]{Y}",
 "\\NewDocumentCommand\\foo{mO{D}}{#1/#2}\\foo{X}/\\foo{Y}[Q]",
 "\\NewDocumentCommand\\foo{s}{#1}\\meaning\\foo"
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
