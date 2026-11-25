import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let cases=[
 "\\NewDocumentEnvironment{foo}{m}{<#1>}{</>}\\begin{foo}{A}X\\end{foo}",
 "\\NewDocumentEnvironment{foo}{O{Z}m}{#1/#2:}{!}\\begin{foo}{A}X\\end{foo}/\\begin{foo}[B]{C}Y\\end{foo}",
 "\\NewDocumentEnvironment{foo}{s}{\\IfBooleanTF{#1}{Y}{N}}{!}\\begin{foo}*X\\end{foo}/\\begin{foo}Z\\end{foo}",
 "\\NewDocumentEnvironment{foo}{}{A}{B}{\\begin{foo}X\\end{foo}}\\begin{foo}Y\\end{foo}",
 "\\NewDocumentEnvironment{foo}{}{A}{B}\\NewDocumentEnvironment{foo}{}{C}{D}\\begin{foo}X\\end{foo}",
 "\\RenewDocumentEnvironment{foo}{}{A}{B}\\begin{foo}X\\end{foo}",
 "\\NewDocumentEnvironment{foo}{}{A}{B}\\RenewDocumentEnvironment{foo}{}{C}{D}\\begin{foo}X\\end{foo}",
 "\\NewDocumentEnvironment{foo}{}{A}{B}\\ProvideDocumentEnvironment{foo}{}{C}{D}\\begin{foo}X\\end{foo}",
 "\\NewDocumentEnvironment{foo}{}{A}{B}\\DeclareDocumentEnvironment{foo}{}{C}{D}\\begin{foo}X\\end{foo}",
 "\\def\\n{foo}\\NewDocumentEnvironment{\\n}{m}{#1}{!}\\begin{foo}{A}X\\end{foo}",
 "\\NewDocumentEnvironment{foo}{d()}{[#1]}{!}\\begin{foo}(A)X\\end{foo}",
 "\\NewDocumentCommand\\foo{m}{A#1}\\NewDocumentCommand\\foo{m}{B#1}\\foo{X}",
 "\\RenewDocumentCommand\\foo{m}{A#1}\\foo{X}",
 "\\NewDocumentCommand\\foo{m}{A#1}\\ProvideDocumentCommand\\foo{m}{B#1}\\foo{X}"
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
