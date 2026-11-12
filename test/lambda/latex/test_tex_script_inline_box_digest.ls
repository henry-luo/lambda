import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import latex: lambda.doc.latex.latex
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
fn expand(source) any^ => profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)
fn check(source) any^ {
 let d=digest.digest(expand(source)^)^;
 {issue:d.issue,matches:if(d.ast==null) false else d.ast==(parse(source,{type:"latex"})^)}
}
[for(source in [
    "\\begin{document}A\\hbox{Hi}B\\end{document}",
    "\\begin{document}A\\vbox{Hi}B\\end{document}",
    "\\begin{document}A\\vtop{Hi}B\\end{document}",
    "\\begin{document}A\\hbox{}B\\end{document}",
    "\\begin{document}A\\hbox to2cm{X}B\\end{document}",
    "\\begin{document}A\\vbox spread2pt{X}B\\end{document}",
    "\\let\\b=\\hbox\\begin{document}A\\b{X}B\\end{document}",
    "\\count0=1\\begin{document}A\\vbox{\\count0=3 X}\\the\\count0 B\\end{document}",
    "\\count0=0\\everyvbox{\\global\\advance\\count0 by1}\\begin{document}A\\vtop{X}\\the\\count0 B\\end{document}",
    "\\begin{document}é\\hbox{藏}你\\end{document}",
    "\\begin{document}A\\vbox{x{y}z}B\\end{document}",
    "\\def\\v{\\vbox{Hi}}\\begin{document}A\\v B\\end{document}",
    "\\begin{document}A\\hbox{Hi there}B\\end{document}"
]) check(source)^];
let d=digest.digest(expand("\\begin{document}A\\hbox{Hi}B\\vbox{There}C\\end{document}")^)^;
{issue:d.issue,html:if(d.ast==null) null else latex.render_to_html(d.ast,{standalone:false})^}
