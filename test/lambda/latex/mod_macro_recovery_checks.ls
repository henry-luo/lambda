import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
pub fn expand(source,options) any^ => profile.expand_with_kernel(source,kernel,options,100000)
pub fn check(source,ast_check=false,mode=false,expl=false) any^ {
    let script=expand(source,{*:options,constructor_argument_mode:mode,script_expl3:expl})^;
    let rebuilt=reconstruct.reconstruct(script)^;
    let native=parse(source,{type:"tex"})^;
    let result=if(ast_check) digest.digest(script)^ else null;
    let native_ast=if(ast_check) parse(source,{type:"latex"})^ else null;
    {issue:script.issue,text_match:rebuilt.text==native.text,
        diagnostics_match:script.diagnostic_records==native.diagnostics,
        digest_issue:if(result==null)null else result.issue,
        ast_match:if(result==null or result.ast==null)null else result.ast==native_ast}
}
