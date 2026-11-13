import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
fn check(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;let n=parse(source,{type:"latex"})^;
 {issue:d.issue,unsupported:d.unsupported,matches:if(d.ast==null)null else d.ast==n,
  actual:if(d.ast==null or d.ast==n)null else d.ast,native:if(d.ast==null or d.ast==n)null else n}
}
[for (source in [
 "\\newcommand{\\q}[2][\\textbf{Z}]{#1/#2}\\begin{document}\\q{B}\\end{document}",
 "\\newcommand{\\q}[2][\\textit{Z}]{#1/#2}\\begin{document}\\q{B}\\end{document}",
 "\\newcommand{\\q}[2][\\emph{\\textbf{Z}}]{#1/#2}\\begin{document}\\q{B}\\end{document}",
 "\\newcommand{\\q}[2][a{b}c]{#1/#2}\\begin{document}\\q{B}\\end{document}",
 "\\newcommand{\\q}[2][a[b]c]{#1/#2}\\begin{document}\\q{B}\\end{document}",
 "\\newcommand{\\q}[2][\\textbf{a[b]c}]{#1/#2}\\begin{document}\\q{B}\\end{document}",
 "\\newcommand{\\q}[2][\\emph{A} B]{#1/#2}\\begin{document}\\q{B}\\end{document}",
 "\\newcommand{\\q}[2][\\textbf{a}\\textit{b}]{#1/#2}\\begin{document}\\q{B}\\end{document}",
 "\\newcommand{\\q}[2][\\foo{a}]{#1/#2}\\begin{document}X\\end{document}",
 "\\newcommand{\\q}[2][a\\%b]{#1/#2}\\begin{document}\\q{B}\\end{document}",
 "\\newcommand{\\q}[2][a% comment\n b]{#1/#2}\\begin{document}\\q{B}\\end{document}",
 "\\newcommand{\\q}[2][\\textbf{é}]{#1/#2}\\begin{document}\\q{B}\\end{document}",
 "\\newcommand{\\q}[2][\\textbf{Z}]{#1/#2}\\begin{document}\\q[X]{B}\\end{document}",
 "\\newenvironment{q}[1][\\textbf{Z}]{#1}{end}\\begin{document}\\begin{q}A\\end{q}\\end{document}",
 "\\newcommand{\\q}[2][\\textbf{Z}]{#1/#2}\\newcommand{\\p}{P}\\begin{document}X\\end{document}",
 "\\newcommand{\\q}[2][{\\textbf{Z}}]{#1/#2}\\begin{document}\\q{B}\\end{document}",
 "\\newcommand{\\q}[2][\\textbf{Z}]{#1/#2}\\begin{document}\\q{}\\end{document}",
 "\\newcommand{\\q}[2][\\textbf{}]{#1/#2}\\begin{document}\\q{B}\\end{document}",
 "\\newcommand{\\q}[2][\\textbf{a}~b]{#1/#2}\\begin{document}X\\end{document}",
 "\\newcommand{\\q}[2][$x+1$]{#1/#2}\\begin{document}X\\end{document}",
 "\\newcommand{\\q}[2][\\emph{A}\\quad B]{#1/#2}\\begin{document}X\\end{document}",
 "\\newcommand{\\q}[2][\\foo{\\textbf{A}}]{#1/#2}\\begin{document}X\\end{document}",
 "\\newcommand{\\q}[2][\\textbf{a\\%b}]{#1/#2}\\begin{document}X\\end{document}",
 "\\newcommand{\\q}[2][ A  B ]{#1/#2}\\begin{document}X\\end{document}",
 "\\newcommand{\\q}[2][\\textbf{ A  B }]{#1/#2}\\begin{document}X\\end{document}",
 "\\newcommand{\\q}[2][A\n B]{#1/#2}\\begin{document}X\\end{document}",
 "\\newcommand{\\q}[2][\\textbf{A}  B]{#1/#2}\\begin{document}X\\end{document}",
 "\\newcommand{\\q}[2][\\textbf { A }]{#1/#2}\\begin{document}X\\end{document}"
]) check(source)^]
