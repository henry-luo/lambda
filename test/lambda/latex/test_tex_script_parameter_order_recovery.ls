import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
fn check(source,ast_check=false) any^ {
    let script=profile.expand_with_kernel(source,kernel,options,100000)^;
    let rebuilt=reconstruct.reconstruct(script)^;
    let native=parse(source,{type:"tex"})^;
    let result=if(ast_check) digest.digest(script)^ else null;
    let native_ast=if(ast_check) parse(source,{type:"latex"})^ else null;
    {issue:script.issue,text_match:rebuilt.text==native.text,
        diagnostics_match:script.diagnostic_records==native.diagnostics,
        digest_issue:if(result==null) null else result.issue,
        ast_match:if(result==null or result.ast==null) null else result.ast==native_ast}
}
[for(source in [
 "\\def\\m#2{<#1>}\\m abc2X", "\\def\\m#0{<#1>}\\m abc0X", "\\def\\m#x{<#1>}\\m abcxX",
 "\\def\\m#1#3{<#1/#2>}\\m AB3X", "\\def\\m#1#1{<#1/#2>}\\m AB1X",
 "\\def\\m#1,#3;{<#1/#2>}\\m A,B3;X", "\\def\\m#2{#2}\\m A2",
 "\\def\\m#x{#0/#1}\\m Ax", "\\def\\m#1#2#3#4#5#6#7#8#9#0{X}\\m123456789X",
 "\\def\\m#1#2#3#4#5#6#7#8#9#0#0{X}\\m123456789X",
 "\\edef\\m#2{<#1>}\\m A2", "\\xdef\\m#x{#2/#1}\\m Ax",
 "\\def\\m#2\n{<#1>}\\m A2", "\\def\\m#2\r{<#1>}\\m A2", "é\\def\\m#2{<#1>}\\m A2",
 "\\def\\outer{\\def\\m#2{X}}\\outer\\m A2", "\\let\\p\\def\\p\\m#2{#2/#1}\\m A2",
 "\\def\\m#{A}\\m X", "\\def\\m#{A}\\m{B}", "\\def\\m#{A}\\meaning\\m"
]) check(source)^];
[for(source in [
 "\\begin{document}\\def\\m#2{<#1>}\\m A2\\end{document}",
 "\\begin{document}\\def\\m#x{#0/#1}\\m Ax\\end{document}",
 "\\begin{document}\\def\\m#1,#3;{<#1/#2>}\\m A,B3;\\end{document}",
 "\\begin{document}\\edef\\m#2{<#1>}\\m A2\\end{document}",
 "\\begin{document}\\xdef\\m#x{#2/#1}\\m Ax\\end{document}",
 "\\begin{document}\\def\\m#{A}\\m X}\\end{document}"
]) check(source,true)^];
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "script-parameter-order-main.tex";
let child=input(root ++ "script-parameter-order-child.tex","text")^;
let script=profile.expand_with_kernel(input(path,"text")^,kernel,
    {source_name:path,echo_definitions:true,adapters:[],
        resources:{local:{'script-parameter-order-child.tex':{data:child}},bundled:{}}},100000)^;
let rebuilt=reconstruct.reconstruct(script)^;
let native=input(path,"tex")^;
let result=digest.digest(script)^;
let native_ast=input(path,"latex")^;
{issue:script.issue,text_match:rebuilt.text==native.text,
    diagnostics_match:script.diagnostic_records==native.diagnostics,
    digest_issue:result.issue,ast_match:if(result.ast==null)null else result.ast==native_ast}
