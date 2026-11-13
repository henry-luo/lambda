import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let baseline=parse("",{type:"tex"})^.messages;
fn check(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:false,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let n=parse(source,{type:"tex"})^;
 let b=reconstruct.reconstruct(r)^;
 {issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==slice(n.messages,len(baseline),len(n.messages))^,diagnostics_match:r.diagnostic_records==n.diagnostics}
}
[for (source in [
 "\\def\\assign{\\count0=3}{\\global\\assign}\\the\\count0",
 "\\def\\assign{\\count0=3}{\\global\\global\\assign}\\the\\count0",
 "\\def\\assign{\\def\\x{A}}{\\global\\assign}\\x",
 "\\def\\assign{\\def\\x##1{##1}}\\long\\assign\\x{A}",
 "\\def\\assign{\\def\\x##1{##1}}\\protected\\assign\\x{A}",
 "\\def\\assign{\\count0=7}\\def\\wrap{\\assign}{\\global\\wrap}\\the\\count0",
 "\\def\\assign{\\let\\a=\\b}\\def\\b{B}{\\global\\assign}\\a",
 "\\def\\assign{\\chardef\\a=65}{\\global\\assign}\\number\\a",
 "\\def\\assign{\\count0=7}{\\globaldefs=-1 \\global\\assign}\\the\\count0",
 "\\def\\assign{\\count0=7}{\\globaldefs=1 \\long\\def\\x{X}\\assign}\\the\\count0\\x",
 "\\def\\assign{\\count0=7}\\expandafter\\global\\expandafter\\assign\\the\\count0",
 "\\protected\\def\\assign{\\count0=7}{\\global\\assign}\\the\\count0",
 "\\def\\assign{\\count0=7}{\\global\\iftrue\\assign\\else\\count0=0\\fi}\\the\\count0", "\\def\\assign{\\count0=9}{\\global\\csname assign\\endcsname}\\the\\count0"
]) check(source)^]

fn ast_check(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:false,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 let n=parse(source,{type:"latex"})^;
 {issue:d.issue,unsupported:d.unsupported,matches:if(d.ast==null)null else d.ast==n,
  actual:if(d.ast==null or d.ast==n)null else d.ast,native:if(d.ast==null or d.ast==n)null else n}
}
[for (source in [
 "\\def\\assign{\\count0=3}{\\global\\assign}\\begin{document}\\the\\count0\\end{document}",
 "\\def\\assign{\\def\\x{A}}\\global\\assign\\begin{document}\\x\\end{document}",
 "\\begin{document}\\def\\assign{\\count0=3}{\\global\\assign}\\the\\count0\\end{document}",
 "\\begin{document}\\def\\assign{\\def\\x{A}}\\global\\assign\\x\\end{document}"
]) ast_check(source)^]
