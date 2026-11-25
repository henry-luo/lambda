import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let cases=[
 "\\NewDocumentCommand{\\foo}{m}{[#1]}\\foo{A}",
 "\\NewDocumentCommand{abc\\foo xyz}{m}{[#1]}\\foo{A}",
 "\\NewDocumentCommand{ \\foo \\bar }{m}{[#1]}\\foo{A}",
 "\\NewDocumentCommand{abc}{m}{[#1]}TAIL",
 "\\NewDocumentCommand{}{m}{[#1]}TAIL",
 "\\NewDocumentCommand X{m}{[#1]}TAIL",
 "\\NewDocumentCommand{{\\foo}}{m}{[#1]}TAIL",
 "\\newcommand{abc\\foo xyz}[1]{[#1]}\\foo{A}",
 "\\newcommand{abc}[1]{[#1]}TAIL",
 "\\newcommand{}[1]{[#1]}TAIL",
 "\\renewcommand{abc}[1]{[#1]}TAIL",
 "\\providecommand{abc}[1]{[#1]}TAIL",
 "\\NewDocumentCommand{\\foo}{m}{[#1]}\\foo{B}",
 "\\NewDocumentCommand{A\\foo B}{s}{#1}\\foo*",
 "\\NewDocumentCommand{\\foo\\bar}{o}{#1}\\foo[Q]"
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
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "definition-target-main.tex";
let child=input(root ++ "definition-target-child.tex","text")^;
let result=profile.expand_with_kernel(input(path,"text")^,kernel,{source_name:path,echo_definitions:true,adapters:[],
 resources:{local:{'definition-target-child.tex':{data:child}},bundled:{}}},100000)^;
let native=input(path,"tex")^;
{text:reconstruct.reconstruct(result)^.text==native.text,
 diagnostics:result.diagnostic_records==native.diagnostics,issue:result.issue}
