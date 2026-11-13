import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
fn check(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,constructor_argument_mode:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 let n=parse(source,{type:"latex"})^;
 {issue:d.issue,unsupported:d.unsupported,matches:if(d.ast==null)null else d.ast==n,
  actual:if(d.ast==null or d.ast==n)null else d.ast,native:if(d.ast==null or d.ast==n)null else n}
}
[for (source in [
 "\\begin{document}X\\end{document}",
 "\\begin{document}X\\end{document}tail",
 "\\begin{document}X\\end{document} ",
 "\\begin{document}X\\end{document}\n",
 "\\begin{document}X\\end{document}\n\n",
 "\\begin{document}X\\end{document}%comment\n",
 "\\ExplSyntaxOn\\begin{document}X\\end{document}\\ExplSyntaxOff"
]) check(source)^]

fn check_tail(tail) any^ { check("\\begin{document}X\\end{document}" ++ tail) }
[for (tail in ["", "a", "a b", " ", "\n", "\n\n", "\n\n\n", "a\n\nb", "a\n\n\nb", " \n\nx", "x\n\n ",
 "a\r\nb", "a\r\n\r\nb", "a\n  \nb", " \t  ", "é Ω 🙃", "[a]& #_^~", "!?:;", "a\n\nb\n\nc", "\n\n\n\n"])
 check_tail(tail)^]
