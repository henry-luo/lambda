import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel = input("lmd/package/doc/latex/tex_engine/kernel.tex", "text")^;
pub fn check(expr) any^ {
    let source = "\\ExplSyntaxOn\\fp_eval:n{" ++ expr ++ "}\\ExplSyntaxOff";
    let result = profile.expand_with_kernel(source,kernel,{script_expl3:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
    let native = parse(source,{type:"tex"})^;
    {expr:expr,issue:result.issue,unsupported:result.unsupported,
     text_match:if(result.issue!=null)null else reconstruct.reconstruct(result)^.text==native.text,
     messages_match:if(result.issue!=null)null else result.messages==native.messages,
     diagnostics_match:if(result.issue!=null)null else result.diagnostic_records==native.diagnostics}
}
