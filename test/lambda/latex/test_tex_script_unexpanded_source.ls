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
let sources=[
 "\\begin{document}A\\unexpanded{abc}B\\end{document}",
 "\\begin{document}\\unexpanded{a\\textbf{X}b}\\end{document}",
 "\\begin{document}\\unexpanded{\\textbf{XY}}\\end{document}",
 "\\begin{document}\\unexpanded{a  b}\\end{document}",
 "\\begin{document}\\unexpanded{a%b\n c}\\end{document}",
 "\\begin{document}\\unexpanded{a\n\nb}\\end{document}",
 "\\begin{document}\\unexpanded{{ab}c}\\end{document}",
 "\\begin{document}\\unexpanded{é文}\\end{document}",
 "\\begin{document}\\unexpanded{a\\&b}\\end{document}",
 "\\def\\x{A}\\begin{document}\\unexpanded{\\x}B\\end{document}",
 "\\def\\x{A}\\begin{document}\\unexpanded{a\\x b}\\end{document}",
 "\\begin{document}\\textbf{\\unexpanded{\\textit{X}}Y}\\end{document}",
 "\\begin{document}$\\unexpanded{x^2}$\\end{document}",
 "\\unexpanded{\\def\\x{A}}\\begin{document}\\x\\end{document}",
 "\\def\\m#1{[#1]}\\begin{document}\\m{\\unexpanded{A B}}\\end{document}"
];
let results=[for(source in sources) check(source)^];
let guard_source="\\begin{document}\\unexpanded{\\unknown{X}}\\end{document}";
let guard_expanded=profile.expand_with_kernel(guard_source,kernel,options,100000)^;
let guard=digest.digest(guard_expanded)^;
{cases:len(results),supported:all([for(r in results) r.supported]),
 text:all([for(r in results) r.text]),diagnostics:all([for(r in results) r.diagnostics]),
 tree:all([for(r in results) r.tree]),guard:guard.issue=="latex-digest-unsupported" and guard.unsupported=="command source mapping"};
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "script-unexpanded-source-main.tex";
let child="script-unexpanded-source-child.tex";
let expanded=profile.expand_with_kernel(input(path,"text")^,kernel,
 {source_name:path,echo_definitions:true,adapters:[],resources:{local:{[child]:{data:input(root ++ child,"text")^}},bundled:{}}},100000)^;
let result=digest.digest(expanded)^; let built=reconstruct.reconstruct(expanded)^;
let native=input(path,"latex")^; let tex=input(path,"tex")^;
{supported:result.issue==null,text:built.text==tex.text,
 diagnostics:expanded.diagnostic_records==tex.diagnostics,tree:result.ast==native}
