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
 "\\def~{X\n \nY}\\begin{document}~\\end{document}",
 "\\def~{X\r\n\r\nY}\\begin{document}~\\end{document}",
 "\\def~{{X\n\nY}}\\begin{document}~\\end{document}",
 "\\def~{X\n\n\\textbf{Y}}\\begin{document}~\\end{document}",
 "\\def~{X%comment\n\nY}\\begin{document}~\\end{document}",
 "\\def~{X\n%comment\nY}\\begin{document}~\\end{document}",
 "\\def~{\n\nX}\\begin{document}~\\end{document}",
 "\\def~{X\n\n}\\begin{document}~\\end{document}",
 "\\def~#1{X\n\n#1}\\begin{document}~{Y}\\end{document}",
 "\\gdef~{X\n\nY}\\begin{document}~\\end{document}",
 "\\long\\def~{X\n\nY}\\begin{document}~\\end{document}",
 "\\catcode`!=13\\def!{X\n\nY}\\begin{document}!\\end{document}",
 "\\begin{document}A\\def~{X\n\nY}~B\\end{document}",
 "\\def~{X\n\nY}\n\n\\begin{document}~\\end{document}",
 "\\def\\m{\\def~{X\n\nY}}\\m\\begin{document}~\\end{document}",
 "\\def\\m{\\def~{X\n\nY}A}\\begin{document}\\m~\\end{document}",
 "\\newenvironment{x}{A\n\nB}{C}\\begin{document}\\begin{x}Q\\end{x}\\end{document}",
 "\\newenvironment {x}{A\n\nB}{C}\\begin{document}\\begin{x}Q\\end{x}\\end{document}",
 "\\newenvironment {x}[1]{A\n\n#1}{C}\\begin{document}\\begin{x}{Q}R\\end{x}\\end{document}",
 "\\newcommand{\\m} {A\n\nB}\\begin{document}\\m\\end{document}",
 "\\newcommand{\\m}[1] {A\n\n#1}\\begin{document}\\m{Q}\\end{document}",
 "\\def~{A\\par B}\\begin{document}~\\end{document}",
 "\\def~{A\n\nB} X\\begin{document}~\\end{document}",
 "\\def\\m{\\def~{A\n\nB}{C}}\\begin{document}\\m~\\end{document}"
];
let results=[for(source in sources) check(source)^];
let guarded_source="\\def~{A\n\nB}{C}\\begin{document}~\\end{document}";
let guarded=digest.digest(profile.expand_with_kernel(guarded_source,kernel,options,100000)^)^;
{cases:len(results),supported:all([for(r in results) r.supported]),
 text:all([for(r in results) r.text]),diagnostics:all([for(r in results) r.diagnostics]),
 tree:all([for(r in results) r.tree]),guard:guarded.issue=="latex-digest-unsupported" and
 guarded.unsupported=="definition following group source mapping"};
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "script-definition-paragraph-main.tex";
let child="script-definition-paragraph-child.tex";
let expanded=profile.expand_with_kernel(input(path,"text")^,kernel,
 {source_name:path,echo_definitions:true,adapters:[],resources:{local:{[child]:{data:input(root ++ child,"text")^}},bundled:{}}},100000)^;
let result=digest.digest(expanded)^; let built=reconstruct.reconstruct(expanded)^;
let native=input(path,"latex")^; let tex=input(path,"tex")^;
{supported:result.issue==null,text:built.text==tex.text,
 diagnostics:expanded.diagnostic_records==tex.diagnostics,tree:result.ast==native}
