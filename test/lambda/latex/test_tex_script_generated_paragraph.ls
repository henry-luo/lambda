import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
fn check(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 let text=reconstruct.reconstruct(r)^;
 let tex=parse(source,{type:"tex"})^;
 let ast=parse(source,{type:"latex"}) ^ {null};
 {source:source,issue:d.issue,unsupported:d.unsupported,text_match:text.text==tex.text,
  matches:if(d.ast==null or ast==null)null else d.ast==ast}
}
let sources=[
 "\\def\\p{\\par}\\begin{document}A\\p B\\end{document}",
 "\\def\\p{\\par}\\begin{document}A\\p\nB\\end{document}",
 "\\def\\p{\\par}\\begin{document}A\\p\n\nB\\end{document}",
 "\\def\\p{\\par}\\begin{document}A\\p%a\nB\\end{document}",
 "\\def\\p{\\par}\\begin{document}A\\p \n%a\nB\\end{document}",
 "\\def\\p{\\par}\\begin{document}A\\p %a\n\nB\\end{document}",
 "\\def\\p{\\par}\\begin{document}A\\p\\p B\\end{document}",
 "\\def\\p{\\par}\\begin{document}A\\p{}B\\end{document}",
 "\\def\\p{\\par}\\begin{document}A\\p\\textbf{B}\\end{document}",
 "\\newenvironment{r}{\\par X}{\\par}\\begin{document}\\begin{r}A\n\\end{r}\nB\\end{document}",
 "\\newenvironment{r}{\\par X}{\\par}\\begin{document}\\begin{r}A\n\\end{r}%x\nB\\end{document}",
 "\\newenvironment{r}{\\par X}{\\par}\\begin{document}\\begin{r}A\n\\end{r}\n\nB\\end{document}"
];
[for(source in sources) check(source)^]
