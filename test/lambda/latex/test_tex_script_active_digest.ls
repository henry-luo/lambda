import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
fn report(label,source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^; let n=parse(source,{type:"latex"})^;
 {case:label,issue:d.issue,unsupported:d.unsupported,matches:if(d.ast == null) null else d.ast == n,script:if(d.ast == n) null else d.ast,native:if(d.ast == n) null else n}
}
report("tilde", "\\begin{document}A~B\\end{document}")
report("repeated", "\\begin{document}~~\\end{document}")
report("nested", "\\begin{document}\\textbf{A~B}\\end{document}")
report("macro", "\\def\\x{~}\\begin{document}A\\x B\\end{document}")
report("defined active", "\\def~{XYZ}\\begin{document}A~B\\end{document}")
report("active punctuation", "\\catcode`!=13\\begin{document}A!B\\end{document}")
report("active letter", "\\catcode`x=13\\begin{document}AxB\\end{document}")
report("active after word", "\\catcode`x=13\\def\\a{\\quad}\\begin{document}\\a x\\end{document}")
report("active control word", "\\catcode`x=13\\begin{document}\\foo x\\end{document}")
fn report_syntax(label,source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^; let native=parse(source,{type:"latex"});
 {case:label,issue:d.issue,unsupported:d.unsupported,native_is_error:native is error}
}
report_syntax("active percent pending", "\\catcode`\\%=13\\begin{document}A%B\\end{document}")
report_syntax("active math shift pending", "\\catcode`\\$=13\\begin{document}A$B\\end{document}")
