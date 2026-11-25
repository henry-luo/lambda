import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let cases=[
 "\\NewDocumentEnvironment{foo}{b}{[#1]}{}\\begin{foo}AB\\end{foo}",
 "\\NewDocumentEnvironment{foo}{b}{[#1]}{!}\\begin{foo}A\\begin{foo}B\\end{foo}C\\end{foo}",
 "\\NewDocumentEnvironment{foo}{b}{[#1]}{}\\begin{foo}A\\begin{bar}B\\end{bar}C\\end{foo}",
 "\\NewDocumentEnvironment{foo}{mb}{#1:[#2]}{!}\\begin{foo}{X}AB\\end{foo}",
 "\\NewDocumentEnvironment{foo}{sb}{\\IfBooleanTF{#1}{Y}{N}[#2]}{}\\begin{foo}*AB\\end{foo}/\\begin{foo}CD\\end{foo}",
 "\\NewDocumentEnvironment{foo}{b}{[#1]}{!}\\begin{foo}\\end{foo}",
 "\\NewDocumentEnvironment{foo}{b}{[#1]}{}\\begin{foo}{A{B}C}\\end{foo}",
 "\\NewDocumentEnvironment{foo}{b}{[#1]}{}\\begin{foo}A\\end{foo}Z",
 "\\NewDocumentEnvironment{foo}{b}{[#1]}{}\\begin{foo}A",
 "\\NewDocumentEnvironment{foo}{b}{[#1]}{}\\begin{foo}\\end{bar}A\\end{foo}"
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
let path=root ++ "xparse-body-main.tex";
let child=input(root ++ "xparse-body-child.tex","text")^;
let result=profile.expand_with_kernel(input(path,"text")^,kernel,{source_name:path,echo_definitions:true,adapters:[],
 resources:{local:{'xparse-body-child.tex':{data:child}},bundled:{}}},100000)^;
let native=input(path,"tex")^;
{text:reconstruct.reconstruct(result)^.text==native.text,
 diagnostics:result.diagnostic_records==native.diagnostics,issue:result.issue}
