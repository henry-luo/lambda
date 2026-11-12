import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
fn expand(source) any^ => profile.expand_with_kernel(source,kernel,
    {echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)
fn check(source) any^ {
 let d=digest.digest(expand(source)^)^;
 {issue:d.issue,matches:if(d.ast==null) false else d.ast==(parse(source,{type:"latex"})^)}
}
[for (source in [
 "\\begin{document}\\'\\`a\\end{document}",
 "\\begin{document}\\'\\v{a}\\end{document}",
 "\\begin{document}\\v\\~\\'{a}\\end{document}",
 "\\begin{document}\\'  \\`é!\\end{document}",
 "\\def\\a{\\'\\`a}\\begin{document}\\a\\end{document}",
 "\\begin{document}\\'{\\'\\`a}\\end{document}"
]) check(source)^];
let gap=digest.digest(expand("\\begin{document}\\'\\`% hi\n{a}\\end{document}")^)^;
[gap.issue,gap.unsupported]
