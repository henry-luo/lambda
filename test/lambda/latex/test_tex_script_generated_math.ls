import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
fn check(source) any^ {
    let expanded=profile.expand_with_kernel(source,kernel,
        {echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
    let result=digest.digest(expanded)^;
    let rebuilt=reconstruct.reconstruct(expanded)^;
    let native_text=parse(source,{type:"tex"})^;
    let native_ast=parse(source,{type:"latex"})^;
    {issue:result.issue,unsupported:result.unsupported,text_match:rebuilt.text==native_text.text,
        ast_match:if(result.ast==null) null else result.ast==native_ast}
}
[for(source in [
    "\\def\\m{\\mathbb{R}}\\begin{document}$\\m^n$\\end{document}",
    "\\def\\m{<}\\begin{document}$a\\m=b$\\end{document}",
    "\\def\\m{=}\\begin{document}$a<\\m b$\\end{document}",
    "\\def\\m{.}\\begin{document}$\\m\\m\\m$\\end{document}",
    "\\def\\m{\\{}\\begin{document}$\\m\\}$\\end{document}",
    "\\def\\m{\\operatorname}\\begin{document}$\\m*{lim}$\\end{document}",
    "\\def\\m{!12}\\begin{document}$\\m$\\end{document}",
    "\\def\\m{}\\begin{document}$a<\\m=b$\\end{document}",
    "\\def\\m{\\frac{1}{2}}\\begin{document}\\(\\m+1\\)\\end{document}",
    "\\def\\m{\\left(x\\right)}\\begin{document}$$\\m$$\\end{document}",
    "\\catcode`\\^=12 \\def\\m{a^2}\\catcode`\\^=7 \\begin{document}$\\m$\\end{document}",
    "\\catcode`!=8 \\def\\m{a!2}\\catcode`!=12 \\begin{document}$\\m$\\end{document}"
]) check(source)^];
let root="test/lambda/latex/fixtures/local_packages/";
let path=root ++ "script-math-gen-main.tex";
let local={'script-math-gen-child-one.tex':{data:input(root ++ "script-math-gen-child-one.tex","text")^},
    'script-math-gen-child-two.tex':{data:input(root ++ "script-math-gen-child-two.tex","text")^}};
let expanded=profile.expand_with_kernel(input(path,"text")^,kernel,
    {source_name:path,echo_definitions:true,adapters:[],resources:{local:local,bundled:{}}},100000)^;
let result=digest.digest(expanded)^;
let rebuilt=reconstruct.reconstruct(expanded)^;
let native_text=input(path,"tex")^;
let native_ast=input(path,"latex")^;
{issue:result.issue,unsupported:result.unsupported,text_match:rebuilt.text==native_text.text,
    ast_match:if(result.ast==null) null else result.ast==native_ast}
