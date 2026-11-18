import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
fn check(source) any^ {
 let ex=profile.expand_with_kernel(source,kernel,options,100000)^;
 let result=digest.digest(ex)^; let built=reconstruct.reconstruct(ex)^;
 let tex=parse(source,{type:"tex"})^; let native=parse(source,{type:"latex"})^;
 {supported:result.issue==null,text:built.text==tex.text,
  diagnostics:ex.diagnostic_records==tex.diagnostics,tree:result.ast==native}
}
let sources = [
 "\\def~{X\n\nY}\\begin{document}~\\end{document}",
 "\\def~{XYZ}\\begin{document}A~B\\end{document}",
 "\\gdef~{XYZ}\\begin{document}A~B\\end{document}",
 "\\edef~{XYZ}\\begin{document}A~B\\end{document}",
 "\\xdef~{XYZ}\\begin{document}A~B\\end{document}",
 "\\catcode`!=13\\def!{XYZ}\\begin{document}A!B\\end{document}",
 "\\catcode`x=13\\def x{Q}\\begin{document}AxB\\end{document}",
 "\\def~#1{#1}\\begin{document}A~{B}C\\end{document}",
 "\\def~{}\\begin{document}A~B\\end{document}",
 "\\def~{A{B}C}\\begin{document}X~Y\\end{document}",
 "\\def~#1/#2{#2#1}\\begin{document}~a/b\\end{document}",
 "\\long\\def~{X}\\begin{document}~\\end{document}",
 "\\global\\def~{X}\\begin{document}~\\end{document}",
 "\\protected\\def~{X}\\begin{document}~\\end{document}",
 "\\begin{document}A\\def~{X}B~C\\end{document}",
 "\\begin{document}\\def~#1{#1}~{X}\\end{document}",
 "\\def~{X}\\def\\a{Y}\\begin{document}~\\a\\end{document}",
 "\\def~{X}%comment\n\\begin{document}A~B\\end{document}",
 "\\catcode`!=13\\long\\def!{X}\\begin{document}A!B\\end{document}",
 "\\begin{document}{A\\def~{X}B}~C\\end{document}",
 "\\begin{document}\\textbf{A\\def~{X}B}~C\\end{document}",
 "\\def\\m{\\def~{X}}\\m\\begin{document}~\\end{document}",
 "\\def\\m{\\def ~{X}}\\m\\begin{document}~\\end{document}",
 "\\def\\m{\\gdef~{X}}\\m\\begin{document}~\\end{document}",
 "\\def\\m{\\long\\def~{X}}\\m\\begin{document}~\\end{document}",
 "\\def\\m{\\def~#1{#1}}\\m\\begin{document}~{X}\\end{document}",
 "\\def\\m{\\def~{X}A}\\begin{document}\\m~\\end{document}",
 "\\def~#1{#1}\\begin{document}~{X}\\end{document}",
 "\\def~{\\textbf{X}}\\begin{document}~\\end{document}",
 "\\def~#1{\\textbf{#1}}\\begin{document}~{X}\\end{document}",
 "\\catcode`!=13\\def!{\\textit{X}}\\begin{document}!\\end{document}"
];
let results=[for(source in sources) check(source)^];
let recovered=check("\\def\\m{\\def~{X}{Y}}\\m\\begin{document}~\\end{document}")^;
{cases:len(results),supported:all([for(r in results) r.supported]),
 text:all([for(r in results) r.text]),diagnostics:all([for(r in results) r.diagnostics]),
 tree:all([for(r in results) r.tree]),following_group:recovered.supported and recovered.text and recovered.diagnostics and recovered.tree};
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "script-active-target-main.tex";
let child="script-active-target-child.tex";
let expanded=profile.expand_with_kernel(input(path,"text")^,kernel,
 {source_name:path,echo_definitions:true,adapters:[],resources:{local:{[child]:{data:input(root ++ child,"text")^}},bundled:{}}},100000)^;
let result=digest.digest(expanded)^; let built=reconstruct.reconstruct(expanded)^;
let native=input(path,"latex")^; let tex=input(path,"tex")^;
{supported:result.issue==null,text:built.text==tex.text,
 diagnostics:expanded.diagnostic_records==tex.diagnostics,tree:result.ast==native}
