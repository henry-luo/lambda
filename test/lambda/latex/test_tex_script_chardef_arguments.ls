import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let prefix="\\chardef\\x=65\\let\\a\\x\\chardef\\z=0\\def\\wrap{\\x}\\begin{document}";
let cases=["\\textbf{\\x}","\\textbf{A\\x B}","\\section{\\x}",
 "\\textit{\\textbf{\\x}}","\\textbf{\\a}","\\textbf{\\x{z}}",
 "\\textbf{\\z}","\\textbf{\\x\\x}","\\textbf{\\x% c\nQ}",
 "\\mystery{\\x}","\\mystery[\\x]{A}","\\mystery{A\\x B}",
 "\\mystery{\\x\\x}","\\mystery{\\textbf{\\x}}",
 "\\mystery{\\x% hi\nQ}","\\mystery{\\x }","\\mystery{\\x  \\x}",
 "\\textbf{\\x }","\\textbf{\\x  \\x}","\\mystery{\\x\n\nQ}","\\mystery{\\x \n\nQ}",
 "\\mystery{\\x\r\n\r\nQ}","\\mystery{\\x% c\n\nQ}","\\textbf{\\wrap}"];
fn check(body) any^ {
 let source=prefix ++ body ++ "\\end{document}";
 let expanded=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let native=parse(source,{type:"tex"})^;
 let tree=parse(source,{type:"latex"})^;
 let result=digest.digest(expanded)^;
 {body:body, issue:result.issue, unsupported:result.unsupported,
  text_match:reconstruct.reconstruct(expanded)^.text==native.text,
  diagnostics_match:expanded.diagnostic_records==native.diagnostics,
  ast_match:if(result.ast==null)null else result.ast==tree}
}
let checked=[for(body in cases)check(body)^];
{cases:len(checked),text_mismatches:len([for(item in checked where not item.text_match) item]),failures:[for(item in checked where not item.diagnostics_match or
 (item.issue==null and not item.ast_match)) item],
 guarded:[for(item in checked where item.issue!=null) {body:item.body,unsupported:item.unsupported}]}
