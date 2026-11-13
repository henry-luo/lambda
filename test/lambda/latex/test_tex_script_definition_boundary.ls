import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
fn check(body) any^ {
 let source="\\begin{document}" ++ body ++ "\\end{document}";
 let r=profile.expand_with_kernel(source,kernel,{constructor_argument_mode:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;let n=parse(source,{type:"latex"})^;
 {body:body,issue:d.issue,unsupported:d.unsupported,matches:if(d.ast==null)null else d.ast==n,actual:if(d.ast==null or d.ast==n)null else d.ast,native:if(d.ast==null or d.ast==n)null else n}
}
[for (body in [
 "\\def\\envname{x}\\renewenvironment{\\envname}{A}{B}X",
 "\\def\\envname{x}X",
 "\\def\\envname{x}\\count0=4 X",
 "\\def\\envname{x}\\newenvironment{\\envname}{A}{B}X",
 "\\def\\envname{x}\\newcommand{\\q}{Y}X",
 "\\def\\envname{x}\\let\\q=\\relax X",
 "\\def\\envname{x}\\relax X",
 "\\def\\envname{x}\\iftrue\\else Y\\fi X",
 "\\def\\envname{x}  X",
 "\\def\\envname{x}%a\nX",
 "\\newcommand{\\q}{A}\\count0=4 X",
 "\\def\\q{A}\\count0=4\\textbf{X}",
 "\\def\\q{A}\\count0=4\\section{T}X",
 "\\def\\q{A}\\count0=4\\begin{quote}X\\end{quote}",
 "\\def\\q{A}\\count0=4\\q",
 "\\def\\q{A}\\count0=4\\char65",
 "\\def\\q{A}\\count0=4\\def\\r{B}X",
 "\\def\\q{A}\\count0=4 X\\q",
 "\\def\\q{A}\\count0=4\\relax  X",
 "\\newenvironment{x}{A}{B}\\count0=4 X",
 "\\newenvironment{x}[1]{A#1}{B}\\count0=4 X",
 "\\def\\q{A}\\count0=4 $x$",
 "\\def\\q{A}\\count0=4 {X}",
 "\\def\\q{A}\\count0=4 \\emph{X}",
 "\\def\\q{A}\\count0=4 \\def\\r{B}\\relax X",
 "\\def\\q{A}\\count0=4 \\par X"
]) check(body)^]
