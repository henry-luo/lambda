import compare: ._tex_script_recovery
import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let expressions=["sin(1e16)","cos(1e16)","sin(-1e16)","cos(-1e16)",
 "sin(1e17)","cos(1e17)","sin(-1e17)","cos(-1e17)",
 "sin(1e18)","cos(1e18)","sin(-1e18)","cos(-1e18)",
 "sin(14148475504056880)","cos(14148475504056880)",
 "sin(1e20)","cos(1e20)","sin(-1e25)","cos(-1e25)","sin(1e30)","cos(1e30)"];
// Native captures preserve an independent reference after the input migration.
let native=input("test/lambda/latex/fixtures/tex_expl3_trig_large_native.json","json")^;
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let cached=compare.preload_expl3({echo_definitions:false})^;
fn check(expr,index) any^ {
 let source="\\ExplSyntaxOn\\fp_eval:n{" ++ expr ++ "}\\ExplSyntaxOff";
 let options={script_expl3:true,adapters:[],resources:{local:{},bundled:{}}};
 let result=profile.expand_with_kernel(source,kernel,options,100000,if(index==0)null else cached)^;
 let reference=native.cases[index];
 {expr:expr,issue:result.issue,unsupported:result.unsupported,
  text_match:if(result.issue!=null)null else source==reference.source and reconstruct.reconstruct(result)^.text==reference.tex.text,
  messages_match:if(result.issue!=null)null else result.messages==reference.tex.messages,
  diagnostics_match:if(result.issue!=null)null else result.diagnostic_records==reference.tex.diagnostics}
}
let results=[for(index,expr in expressions) check(expr,index)^];
{cases:len(results),failures:[for(result in results where result.issue!=null or not result.text_match or
 not result.messages_match or not result.diagnostics_match) result]}
