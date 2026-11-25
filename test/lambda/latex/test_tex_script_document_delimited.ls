import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let cases=[
 "\\NewDocumentCommand\\foo{d()}{\\IfNoValueTF{#1}{N}{[#1]}}\\foo/\\foo(A)",
 "\\NewDocumentCommand\\foo{D(){Z}m}{#1/#2}\\foo X/\\foo(A){B}",
 "\\NewDocumentCommand\\foo{d<>}{[#1]}\\foo<a<b>c>/\\foo",
 "\\NewDocumentCommand\\foo{d||}{[#1]}\\foo|ABC|/\\foo",
 "\\NewDocumentCommand\\foo{D<>{Z}}{[#1]}\\foo<{A>B}>/\\foo",
 "\\NewDocumentCommand\\foo{d()}{[#1]}\\foo()/\\foo(AB)",
 "\\NewDocumentCommand\\foo{D()Z}{[#1]}\\foo/\\foo(A)",
 "\\NewDocumentCommand\\foo{O Z}{[#1]}\\foo/\\foo[A]",
 "\\NewDocumentCommand\\foo{G Z}{[#1]}\\foo/\\foo{A}",
 "\\NewDocumentCommand\\foo{sd()m}{#1/#2/#3}\\foo*(A){B}/\\foo{C}",
 "\\NewDocumentCommand\\foo{d<>}{[#1]}\\foo <a<b<c>d>e>",
 "\\NewDocumentCommand\\foo{d()}{[#1]}\\foo({x)y}z)",
 "\\NewDocumentCommand\\foo{d()}{[#1]}\\foo(unfinished",
 "\\NewDocumentCommand\\foo{r()}{[#1]}\\foo(A)/\\foo X",
 "\\NewDocumentCommand\\foo{R(){Z}}{[#1]}\\foo(B)/\\foo X",
 "\\NewDocumentCommand\\foo{r<>r()}{#1/#2}\\foo X",
 "\\NewDocumentCommand\\foo{r()}{#1}\\foo",
 "\\NewDocumentCommand\\foo{r()}{#1}\\foo(A"
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
let path=root ++ "xparse-required-main.tex";
let child=input(root ++ "xparse-required-child.tex","text")^;
let result=profile.expand_with_kernel(input(path,"text")^,kernel,{source_name:path,echo_definitions:true,adapters:[],
 resources:{local:{'xparse-required-child.tex':{data:child}},bundled:{}}},100000)^;
let native=input(path,"tex")^;
{text:reconstruct.reconstruct(result)^.text==native.text,
 diagnostics:result.diagnostic_records==native.diagnostics,issue:result.issue}
