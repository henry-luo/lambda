import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
fn safe_digestion(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 if(d.ast!=null) d.ast==(parse(source,{type:"latex"})^) else d.issue=="latex-digest-unsupported"
}
[for (source in [
  "\\begin{document}\\meaning\\section\\end{document}",
  "\\dimendef\\d=3 \\begin{document}\\meaning\\d\\end{document}",
  "\\skipdef\\s=5 \\begin{document}\\meaning\\s\\end{document}",
  "\\begin{document}A\\meaning\\% B\\end{document}",
  "\\begin{document}\\string\\foo\\end{document}",
  "\\begin{document}\\string\\%\\end{document}",
  "\\begin{document}\\string A\\end{document}",
  "\\begin{document}\\meaning A\\end{document}",
  "\\begin{document}\\string $\\end{document}",
  "\\begin{document}\\string #\\end{document}",
  "\\escapechar=-1 \\begin{document}\\string\\foo\\end{document}"
]) safe_digestion(source)^]

fn ordinary_digestion(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 d.ast!=null and d.ast==(parse(source,{type:"latex"})^)
}
[for(source in ["\\begin{document}\\string A\\end{document}","\\begin{document}\\meaning A\\end{document}","\\escapechar=-1 \\begin{document}\\string\\foo\\end{document}"]) ordinary_digestion(source)^]
