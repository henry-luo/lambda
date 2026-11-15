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
 "\\def\\m{#12}A\\m B", "\\def\\m#1{#2/#0/#x/#}A\\m{Q}B",
 "\\def\\m{##1}A\\m B", "\\def\\m{###1}A\\m B",
 "\\def\\m{#a#b}\\m", "\\def\\m{A\n#1\nB}\\m",
 "\\def\\m{A\r#1\rB}\\m", "\\def\\m{A\r\n#1\r\nB}\\m",
 "é\\def\\m{#1}\\m", "\\def\\m{#{X}}\\m", "\\def\\m#1{#1#2#1}\\m{Q}",
 "\\gdef\\m{#1}\\m", "\\edef\\m{#1}\\m", "\\xdef\\m{#2/#}\\m",
 "\\edef\\m{A\n#1\nB}\\m", "\\newcommand{\\m}{#1}A\\m B",
 "\\newcommand{\\m}[1]{#2/#0/#x/#}A\\m{Q}B", "\\newcommand{\\m}{#}\\m",
 "\\newcommand{\\b}{#1}A", "\\providecommand{\\b}{#1}A",
 "\\renewcommand{\\never}{#1}\\never", "\\renewcommand{\\never}[12]{#1}\\never",
 "\\catcode`1=11 \\newcommand{\\q}[1]{#1}\\q{A}"
]) check(source)^];
[for(source in [
 "\\begin{document}\\def\\m{#1}A\\m B\\end{document}",
 "\\begin{document}\\def\\m#1{#2/#0/#x/#}\\m{Q}\\end{document}",
 "\\def\\m{#1}\\begin{document}$\\m$\\end{document}",
 "\\begin{document}\\edef\\m{#1}A\\m B\\end{document}",
 "\\begin{document}\\xdef\\m{#2/#}\\m\\end{document}",
 "\\begin{document}\\newcommand{\\m}{#1}\\m\\end{document}",
 "\\begin{document}\\newcommand{\\m}[1]{#2/#}\\m{A}\\end{document}"
]) check(source,true)^];
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "script-illegal-param-main.tex";
let child=input(root ++ "script-illegal-param-child.tex","text")^;
let script=profile.expand_with_kernel(input(path,"text")^,kernel,
    {source_name:path,echo_definitions:true,adapters:[],
        resources:{local:{'script-illegal-param-child.tex':{data:child}},bundled:{}}},100000)^;
let rebuilt=reconstruct.reconstruct(script)^;
let native=input(path,"tex")^;
let result=digest.digest(script)^;
let native_ast=input(path,"latex")^;
{issue:script.issue,text_match:rebuilt.text==native.text,
    diagnostics_match:script.diagnostic_records==native.diagnostics,
    digest_issue:result.issue,ast_match:if(result.ast==null)null else result.ast==native_ast}

let guarded=profile.expand_with_kernel("\\def\\z{}\\edef\\m{#\\z 1}\\m",kernel,options,100000)^;
{changed_token_stream_issue:guarded.issue}
