import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let sources=["\\begin{document}A\\the\\nullfont B\\end{document}",
 "\\begin{document}$x\\the\\nullfont y$\\end{document}",
 "\\begin{document}A\\textbf{B\\the\\nullfont C}D\\end{document}",
 "\\edef\\x{\\the\\nullfont}\\begin{document}A\\x B\\end{document}",
 "\\begin{document}A\\the\\font B\\end{document}",
 "\\begin{document}$x\\the\\textfont0 y$\\end{document}",
 "\\font\\f=cmr10 at 13pt\\begin{document}A\\the\\f B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\f\\begin{document}A\\textbf{\\the\\font}B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\font\\g=cmr10 at 13pt\\def\\f{ZZ}\\begin{document}A\\the\\g B\\end{document}"];
fn check(source) any^ {
 let result=profile.expand_with_kernel(source,kernel,{constructor_argument_mode:true,echo_definitions:true,
  adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let parsed=digest.digest(result)^; let native=parse(source,{type:"tex"})^;
 {source:source,issue:parsed.issue,unsupported:parsed.unsupported,
  text_match:reconstruct.reconstruct(result)^.text==native.text,
  diagnostics_match:result.diagnostic_records==native.diagnostics,
  messages_match:result.messages==native.messages,
  ast_match:if(parsed.ast==null)null else parsed.ast==parse(source,{type:"latex"})^}
}
let checked=[for(source in sources) check(source)^];
{cases:len(checked),failures:[for(x in checked where x.issue!=null or not x.text_match or
 not x.diagnostics_match or not x.messages_match or not x.ast_match) x]}
let source="\\begin{document}A\\textbf{B\\the\\nullfont C}D\\end{document}";
let legacy=profile.expand_with_kernel(source,kernel,{constructor_argument_mode:false,echo_definitions:true,
 adapters:[],resources:{local:{},bundled:{}}},100000)^;
let guarded=digest.digest(legacy)^;
{issue:guarded.issue,unsupported:guarded.unsupported}
