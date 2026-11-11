import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
fn report(label,math) any^ {
 let source="\\begin{document}$" ++ math ++ "$\\end{document}";
 let r=profile.expand_with_kernel(source,kernel,{resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 let n=parse(source,{type:"latex"})^;
 {case:label,issue:d.issue,unsupported:d.unsupported,matches:if(d.ast == null) null else d.ast == n,script:if(d.ast == n) null else d.ast,native:if(d.ast == n) null else n}
}
report("blackboard", "\\mathbb{R}")
report("bold", "\\mathbf{v}")
report("roman", "\\mathrm{ab}")
report("italic", "\\mathit{x}")
report("sans", "\\mathsf{x}")
report("sans italic", "\\mathsfit{x}")
report("typewriter", "\\mathtt{x}")
report("calligraphic", "\\mathcal{F}")
report("fraktur", "\\mathfrak{g}")
report("script", "\\mathscr{F}")
report("normal", "\\mathnormal{x}")
report("boldsymbol", "\\boldsymbol{\\alpha}")
report("unbraced", "\\mathbf x")
report("nested", "\\mathbf{\\mathit{x}}");
report("scripted", "\\mathbb{R}^n")
report("membership", "x\\in\\mathbb{R}")
