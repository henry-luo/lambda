import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
pub fn check(source,constructor) any^ {
 let result=profile.expand_with_kernel(source,kernel,{constructor_argument_mode:constructor,echo_definitions:true,
  adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let parsed=digest.digest(result)^;let native=parse(source,{type:"tex"})^;
 {source:source,constructor:constructor,issue:parsed.issue,unsupported:parsed.unsupported,
  text:reconstruct.reconstruct(result)^.text==native.text,
  diagnostics:result.diagnostic_records==native.diagnostics,messages:result.messages==native.messages,
  ast:if(parsed.ast==null)null else parsed.ast==parse(source,{type:"latex"})^}
}
