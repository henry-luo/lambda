import compare: ._tex_script_math_unicode_compare
import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let sources=["\\def\\x{é}\\begin{document}$\\x α$\\end{document}",
 "\\def\\x{éβ}\\begin{document}$\\x^2$\\end{document}",
 "\\def\\x{é}\\def\\y{β}\\begin{document}$\\x\\y$\\end{document}",
 "\\begin{document}$é\\char945$\\end{document}",
 "\\begin{document}$\\vcenter{éβ}^2$\\end{document}"];
let checked=[for(source in sources,constructor in [false,true]) compare.check(source,constructor)^];
{cases:len(checked),failures:[for(x in checked where x.issue!=null or not x.text or not x.diagnostics or not x.messages or x.ast!=true) x]}
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "script_vcenter_main.tex";
let source=input(path,"text")^;
let child=input(root ++ "script_vcenter_child.tex","text")^;
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
fn check_file(constructor) any^ {
 let result=profile.expand_with_kernel(source,kernel,{source_name:path,constructor_argument_mode:constructor,
  echo_definitions:true,adapters:[],resources:{local:{'script_vcenter_child.tex':{data:child}},bundled:{}}},100000)^;
 let parsed=digest.digest(result)^;let native=input(path,"tex")^;
 {constructor:constructor,issue:parsed.issue,text:reconstruct.reconstruct(result)^.text==native.text,
  diagnostics:result.diagnostic_records==native.diagnostics,
  ast:if(parsed.ast==null)null else parsed.ast==input(path,"latex")^}
}
[check_file(false)^,check_file(true)^]
let unbraced="\\begin{document}$x^é$\\end{document}";
let expanded=profile.expand_with_kernel(unbraced,kernel,{echo_definitions:true,adapters:[],
 resources:{local:{},bundled:{}}},100000)^;
let unbraced_result=digest.digest(expanded)^;
{issue:unbraced_result.issue,unsupported:unbraced_result.unsupported}
