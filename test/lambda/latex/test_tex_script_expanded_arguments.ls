import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
fn check(source,mode) any^ {
 let ex=profile.expand_with_kernel(source,kernel,{*:options,constructor_argument_mode:mode},100000)^;
 let d=digest.digest(ex)^; let tex=parse(source,{type:"tex"})^; let native=parse(source,{type:"latex"})^;
 {supported:ex.issue==null and d.issue==null,text:reconstruct.reconstruct(ex)^.text==tex.text,
  messages:ex.messages==tex.messages,diagnostics:ex.diagnostic_records==tex.diagnostics,tree:d.ast==native}
}
let sources=[
 "\\def\\a{ABC}\\begin{document}\\unknown{\\a}\\end{document}",
 "\\def\\a{}\\begin{document}\\unknown{x\\a y}\\end{document}",
 "\\def\\a{ABC}\\begin{document}\\unknown[\\a]{\\a}\\end{document}",
 "\\def\\a{ABC}\\begin{document}\\unknown(\\a)\\end{document}",
 "\\def\\a{ABC}\\begin{document}\\unknown*{\\a}\\end{document}",
 "\\def\\a{ABC}\\begin{document}\\unknown{pre\\a post}\\end{document}",
 "\\def\\a{ABC}\\begin{document}\\unknown{a{\\a}b}\\end{document}",
 "\\def\\a{ABC}\\begin{document}\\unknown{\\textbf{\\a}}\\end{document}",
 "\\def\\a{ABC}\\begin{document}\\unknown{\\#\\a\\&}\\end{document}",
 "\\def\\a{é}\\begin{document}\\unknown{é\\a}\\end{document}",
 "\\def\\a{ABC}\\begin{document}\\unknown% comment\n{\\a}\\end{document}",
 "\\def\\a{ABC}\\begin{document}\\unknown{\\a}{tail}\\unknown{\\a}\\end{document}"
];
let results=[for(source in sources,mode in [false,true]) check(source,mode)^];
{cases:len(results),supported:all([for(r in results) r.supported]),text:all([for(r in results) r.text]),
 messages:all([for(r in results) r.messages]),diagnostics:all([for(r in results) r.diagnostics]),tree:all([for(r in results) r.tree])};
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "script-expanded-arg-main.tex";
let child="script-expanded-arg-child.tex";
[for(mode in [false,true]) {
 let ex=profile.expand_with_kernel(input(path,"text")^,kernel,{*:options,source_name:path,constructor_argument_mode:mode,
  resources:{local:{[child]:{data:input(root ++ child,"text")^}},bundled:{}}},100000)^;
 let d=digest.digest(ex)^; let tex=input(path,"tex")^; let native=input(path,"latex")^;
 {supported:ex.issue==null and d.issue==null,text:reconstruct.reconstruct(ex)^.text==tex.text,
  messages:ex.messages==tex.messages,diagnostics:ex.diagnostic_records==tex.diagnostics,tree:d.ast==native}
}]
