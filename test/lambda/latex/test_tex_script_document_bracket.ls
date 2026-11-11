import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
fn report(label,source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;let n=parse(source,{type:"latex"})^;
 {case:label,issue:d.issue,unsupported:d.unsupported,matches:if(d.ast == null) null else d.ast == n,script:if(d.ast == n) null else d.ast,native:if(d.ast == n) null else n}
}
report("leading bracket", "\\begin{document}[A]\\end{document}")
report("multiple brackets", "\\begin{document}[A][B]\\end{document}")
report("nested content", "\\begin{document}[A {B} C]\\end{document}")
report("followed by group", "\\begin{document}[A]{B}\\end{document}")
report("macro result", "\\def\\v{[A]}\\begin{document}\\v\\end{document}")
report("ordinary environment still pending", "\\begin{document}\\begin{strange}[x]Hello\\end{strange}\\end{document}")
