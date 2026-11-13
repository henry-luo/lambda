import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let baseline=parse("",{type:"tex"})^.messages;
fn check(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let n=parse(source,{type:"tex"})^;let b=reconstruct.reconstruct(r)^;
 {source:source,issue:r.issue,unsupported:r.unsupported,reconstruct_issue:b.issue,text_match:if(r.issue==null)b.text==n.text else null,
  messages_match:r.messages==slice(n.messages,len(baseline),len(n.messages))^,diagnostics_match:r.diagnostic_records==n.diagnostics}
}
[for (source in [
 "\\ifnum1=1\\else Z\\fi X", "\\ifnum1=2\\else Z\\fi X",
 "\\ifnum1=1\\fi X", "\\ifnum1=2\\fi X",
 "\\ifnum1=1 \\else Z\\fi X", "\\ifnum1=2 \\else Z\\fi X",
 "\\ifnum1=1 A\\else Z\\fi X", "\\ifnum1=2 A\\else Z\\fi X",
 "\\ifnum-1<0\\else Z\\fi X", "\\ifnum-1>0\\else Z\\fi X",
 "\\ifodd1\\else Z\\fi X", "\\ifodd2\\else Z\\fi X",
 "\\ifodd1\\fi X", "\\ifodd2\\fi X",
 "\\ifdim1pt=1pt\\else Z\\fi X", "\\ifdim1pt=2pt\\else Z\\fi X",
 "\\ifdim1pt<2pt\\fi X", "\\ifdim2pt<1pt\\fi X",
 "\\unless\\ifnum1=1\\else Z\\fi X", "\\unless\\ifnum1=2\\else Z\\fi X",
 "\\iftrue\\ifnum1=1\\else Z\\fi A\\else B\\fi X",
 "\\iftrue\\ifnum1=2\\else Z\\fi A\\else B\\fi X",
 "\\iffalse A\\else\\ifodd1\\else Z\\fi B\\fi X",
 "\\def\\n{1}\\ifnum\\n=1\\else Z\\fi X",
 "\\def\\n{2}\\ifnum1=\\n\\else Z\\fi X",
 "\\let\\e=\\else\\ifnum1=1\\e Z\\fi X",
 "\\let\\f=\\fi\\ifnum1=1\\f X",
 "\\ifnum1=\\iftrue1\\else2\\fi\\else Z\\fi X",
 "\\ifnum1=\\iftrue2\\else1\\fi\\else Z\\fi X",
 "\\ifnum1=1\\ifodd2 A\\else B\\fi\\else C\\fi X",
 "\\ifodd\\numexpr2+1\\relax\\else Z\\fi X",
 "\\ifnum1=1\\ifodd1 A\\else B\\fi\\else C\\fi X",
 "\\ifnum1=2\\ifodd1 A\\else B\\fi\\else C\\fi X",
 "\\ifnum1=2\\ifodd2 A\\else B\\fi\\else C\\fi X",
 "\\ifodd1\\ifnum2=1 A\\else B\\fi\\else C\\fi X",
 "\\ifodd2\\ifnum2=1 A\\else B\\fi\\else C\\fi X",
 "\\ifnum1=1\\iftrue\\iffalse A\\else B\\fi\\fi\\else C\\fi X",
 "\\ifnum1=2\\iftrue\\iffalse A\\else B\\fi\\fi\\else C\\fi X",
 "\\unless\\ifnum1=1\\ifodd2 A\\else B\\fi\\else C\\fi X",
 "\\unless\\ifnum1=2\\ifodd2 A\\else B\\fi\\else C\\fi X",
 "\\def\\n{\\ifodd2 A\\else B\\fi}\\ifnum1=1\\n\\else C\\fi X",
 "\\ifnum1=1\\noexpand\\else Z\\fi X",
 "\\ifnum1=2\\noexpand\\else Z\\fi X",
 "\\ifnum1=1\\expandafter\\else\\fi X",
 "\\ifnum1=1\\ifcase0 A\\or B\\fi\\else C\\fi X",
 "\\ifnum1=2\\ifcase0 A\\or B\\fi\\else C\\fi X",
 "\\ifnum1=1\\ifcase1 A\\or B\\fi\\else C\\fi X",
 "\\ifnum1=1\\iftrue\\iftrue A\\else B\\fi\\else C\\fi\\else D\\fi X",
 "\\ifnum1=2\\iftrue\\iftrue A\\else B\\fi\\else C\\fi\\else D\\fi X",
 "\\def\\x{Y}\\count0=1\\noexpand\\x X",
 "\\def\\x{Y}\\ifnum1=1\\noexpand\\x\\else Z\\fi X",
 "\\def\\x{Y}\\ifodd1\\noexpand\\x\\else Z\\fi X",
 "\\let\\e=\\else\\ifnum1=1\\noexpand\\e Z\\fi X",
 "\\ifcase0\\or A\\fi X",
 "\\ifcase1\\or A\\fi X",
 "\\ifcase0\\else A\\fi X",
 "\\ifcase1\\else A\\fi X",
 "\\ifcase0\\fi X",
 "\\ifcase0\\or A\\or B\\else C\\fi X",
 "\\ifcase1\\or A\\or B\\else C\\fi X",
 "\\ifcase2\\or A\\or B\\else C\\fi X",
 "\\ifcase3\\or A\\or B\\else C\\fi X",
 "\\ifcase-1\\or A\\else C\\fi X",
 "\\ifcase0\\noexpand\\or A\\fi X",
 "\\ifcase1\\noexpand\\or A\\fi X",
 "\\ifcase0\\expandafter\\or\\fi X",
 "\\ifcase0\\ifodd1 A\\else B\\fi\\or C\\fi X",
 "\\ifcase1\\ifodd1 A\\else B\\fi\\or C\\fi X",
 "\\ifcase0\\ifodd2 A\\else B\\fi\\or C\\fi X",
 "\\ifcase1\\ifodd2 A\\else B\\fi\\or C\\fi X",
 "\\iftrue\\ifcase1\\or A\\else B\\fi C\\else D\\fi X",
 "\\def\\n{1}\\ifcase\\n\\or A\\else B\\fi X",
 "\\let\\o=\\or\\ifcase1\\o A\\fi X"
]) check(source)^]

fn check_ast(body) any^ {
 let source="\\begin{document}" ++ body ++ "\\end{document}";
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;let n=parse(source,{type:"latex"})^;
 {body:body,issue:d.issue,unsupported:d.unsupported,matches:if(d.ast==null)null else d.ast==n,actual:if(d.ast==null or d.ast==n)null else d.ast,native:if(d.ast==null or d.ast==n)null else n}
}
[for (body in [
 "\\ifnum1=1\\else Z\\fi X", "\\ifnum1=2\\else Z\\fi X",
 "\\ifnum1=1\\fi X", "\\ifodd2\\else Z\\fi X",
 "\\ifdim1pt=1pt\\else Z\\fi X", "\\ifdim1pt=2pt\\else Z\\fi X",
 "\\ifnum1=1\\ifodd2 A\\else B\\fi\\else C\\fi X",
 "\\ifnum1=2\\ifodd2 A\\else B\\fi\\else C\\fi X",
 "\\ifnum1=1\\noexpand\\else Z\\fi X", "\\ifnum1=1\\expandafter\\else\\fi X",
 "\\def\\x{Y}\\ifnum1=1\\noexpand\\x\\else Z\\fi X"
]) check_ast(body)^]
