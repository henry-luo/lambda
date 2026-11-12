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
[for(source in [
    "\\begin{document}A\\hrule!B\\end{document}",
    "\\begin{document}A\\vrule!B\\end{document}",
    "\\begin{document}A\\hrule width1pt height2pt depth3pt!B\\end{document}",
    "\\def\\r{\\vrule}\\begin{document}A\\r!B\\end{document}",
    "\\begin{document}A\\vrule depth2cm width3cm height4cm!B\\end{document}",
    "\\begin{document}A\\hrule height1cm height2cm!B\\end{document}",
    "\\def\\w{2pt}\\begin{document}A\\hrule width\\w!B\\end{document}",
    "\\begin{document}é\\hrule width 2cm!你\\end{document}",
    "\\begin{document}A\\vrule width-1pt height-2pt depth3pt!B\\end{document}",
    "\\begin{document}A\\hrule width1cm!B\\end{document}",
    "\\begin{document}A\\hrule why!B\\end{document}",
    "\\begin{document}A\\hrule  width 1cm!B\\end{document}",
    "\\let\\r=\\vrule\\begin{document}A\\r width2cm!B\\end{document}",
    "\\dimen0=2pt\\begin{document}A\\hrule height\\dimen0!B\\end{document}",
    "\\begin{document}A\\vrule width\\dimexpr1pt+2pt\\relax!B\\end{document}",
    "\\def\\r{\\hrule width2pt height1pt}\\begin{document}A\\r!B\\end{document}",
    "\\begin{document}A\\hrule\\end{document}"
]) check(source)^];
let d=digest.digest(expand("\\begin{document}A\\hrule width1pt height2pt!B\\end{document}")^)^;
{issue:d.issue,html:if(d.ast==null) null else latex.render_to_html(d.ast,{standalone:false})^}
