import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import latex: lambda.doc.latex.latex
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
fn expand(source) any^ => profile.expand_with_kernel(source,kernel,
    {echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)
fn check(source) any^ {
 let d=digest.digest(expand(source)^)^;
 {issue:d.issue,matches:if(d.ast==null) false else d.ast==(parse(source,{type:"latex"})^)}
}
fn rejected(source) any^ {
 let d=digest.digest(expand(source)^)^;
 [d.issue,d.unsupported]
}
[for (name in ["'", "`", "^", "\"", "~", "=", ".", "u", "v", "H", "c", "d", "b", "t", "k"])
    check("\\begin{document}\\" ++ name ++ "{a}\\end{document}")^];
[for (source in [
 "\\begin{document}\\accent19 e\\end{document}",
 "\\begin{document}\\accent94 a\\end{document}",
 "\\begin{document}\\'{e} \\`a\\end{document}",
 "\\begin{document}\\v{c} \\H{o} \\~n\\end{document}",
 "\\begin{document}\\'{é} \\`你\\end{document}",
 "\\begin{document}a\\'  {e} b\\end{document}",
 "\\begin{document}\\'{\\textbf{x}}\\end{document}",
 "\\def\\foo{\\'{a}}\\begin{document}\\foo\\end{document}",
 "\\def\\a#1{\\'{#1}}\\begin{document}\\a{x}Z\\end{document}",
 "\\def\\a{\\'e}\\begin{document}\\a Z\\end{document}",
 "\\begin{document}\\'{}\\end{document}",
 "\\begin{document}\\'abc\\end{document}",
 "\\begin{document}\\v  e!\\end{document}"
]) check(source)^];
[for (source in [
 "\\begin{document}\\'% hi\n{e}\\end{document}",
 "\\begin{document}\\v% hi\n{e}\\end{document}",
 "\\begin{document}\\'\\i!\\end{document}",
 "\\begin{document}\\'\\%\\end{document}",
 "\\begin{document}\\accent19 \\char123!\\end{document}"
]) rejected(source)^]
;
let rendered = digest.digest(expand("\\begin{document}\\accent19 e \\`a \\v{c}\\end{document}")^)^;
{issue: rendered.issue, html: if (rendered.ast == null) null else latex.render_to_html(rendered.ast, {standalone:false})^}
