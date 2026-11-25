import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let sources=["\\begin{document}$a\\vcenter{b}c$\\end{document}",
 "\\begin{document}$a\\vcenter to 2pt{b}c$\\end{document}",
 "\\begin{document}$a\\vcenter spread 2pt{b}c$\\end{document}",
 "\\begin{document}$a\\vcenter{\\frac{1}{2}}b$\\end{document}",
 "\\begin{document}$\\vcenter{}$\\end{document}",
 "\\begin{document}$\\vcenter{x+y}$\\end{document}",
 "\\begin{document}$\\vcenter{x}^{2}$\\end{document}",
 "\\begin{document}$\\vcenter{\\vcenter{x}}$\\end{document}",
 "\\def\\v{\\vcenter{x}}\\begin{document}$a\\v b$\\end{document}",
 "\\begin{document}$\\vcenter{A{B}C}\\vcenter{z}$\\end{document}"];
fn check(source, constructor) any^ {
 let result=profile.expand_with_kernel(source,kernel,{constructor_argument_mode:constructor,echo_definitions:true,
  adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let parsed=digest.digest(result)^;let native=parse(source,{type:"tex"})^;
 let native_ast=parse(source,{type:"latex"}) ^ { null };
 {source:source,constructor:constructor,issue:parsed.issue,unsupported:parsed.unsupported,
  text_match:reconstruct.reconstruct(result)^.text==native.text,
  diagnostics_match:result.diagnostic_records==native.diagnostics,
  messages_match:result.messages==native.messages,
  ast_match:if(parsed.ast==null or native_ast==null)null else parsed.ast==native_ast,
  actual:if(parsed.ast==null or native_ast==null or parsed.ast==native_ast)null else parsed.ast,
  native:if(parsed.ast==null or native_ast==null or parsed.ast==native_ast)null else native_ast}
}
let checked=[for(source in sources, constructor in [false,true]) check(source,constructor)^];
{cases:len(checked),failures:[for(x in checked where x.issue!=null or not x.text_match or
 not x.diagnostics_match or not x.messages_match or x.ast_match!=true) x]}
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "vcenter-math-main.tex";
let source=input(path,"text")^;
let child=input(root ++ "vcenter-math-child.tex","text")^;
fn check_file(constructor) any^ {
 let result=profile.expand_with_kernel(source,kernel,{source_name:path,constructor_argument_mode:constructor,
  echo_definitions:true,adapters:[],resources:{local:{'vcenter-math-child.tex':{data:child}},bundled:{}}},100000)^;
 let parsed=digest.digest(result)^;let native=input(path,"tex")^;
 {constructor:constructor,issue:parsed.issue,text:reconstruct.reconstruct(result)^.text==native.text,
  diagnostics:result.diagnostic_records==native.diagnostics,
  ast:if(parsed.ast==null)null else parsed.ast==input(path,"latex")^}
}
[check_file(false)^,check_file(true)^]
