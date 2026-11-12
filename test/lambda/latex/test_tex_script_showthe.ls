import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let baseline=parse("",{type:"tex"})^.messages;
fn check(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let n=parse(source,{type:"tex"})^;
 let b=reconstruct.reconstruct(r)^;
 {issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==slice(n.messages,len(baseline),len(n.messages))^,diagnostics_match:r.diagnostic_records==n.diagnostics}
}
[for (source in [
  "\\count0=12 \\showthe\\count0",
  "\\count0=-123 \\showthe\\count0 X",
  "\\dimen0=1.5pt \\showthe\\dimen0",
  "\\skip0=1pt plus 2pt minus 3pt \\showthe\\skip0",
  "\\muskip0=2mu plus 1mu \\showthe\\muskip0",
  "\\toks0={abc} \\showthe\\toks0",
  "\\toks0={} \\showthe\\toks0 X",
  "\\def\\foo{XYZ}\\toks0={\\foo #1} \\showthe\\toks0",
  "\\countdef\\a=10 \\a=3 \\showthe\\a",
  "\\dimendef\\a=10 \\a=3pt \\showthe\\a",
  "\\skipdef\\a=10 \\a=3pt \\showthe\\a",
  "\\muskipdef\\a=10 \\a=3mu \\showthe\\a",
  "\\toksdef\\a=10 \\a={hi} \\showthe\\a",
  "\\showthe\\numexpr2+3\\relax",
  "\\showthe\\dimexpr2pt+3pt\\relax",
  "\\showthe\\catcode`A",
  "\\showthe\\escapechar",
  "\\showthe\\inputlineno",
  "\\showthe\\eTeXversion",
  "\\message{before}\\showthe\\count0\\message{after}",
  "\\let\\s=\\showthe \\s\\count0",
  "\\iftrue\\showthe\\count0\\else nope\\fi",
  "\\showthe\\font",
  "\\font\\f=cmr10 \\showthe\\f",
  "\\toks0={\\relax \\textbf{x}}\\showthe\\toks0",
  "\\newlinechar=65 \\toks0={A} \\showthe\\toks0",
  "\\begin{document}A\\showthe\\count0 B\\end{document}",
  "\\count0=3 \\count3=7 \\showthe\\count\\the\\count0",
  "\\def\\n{3}\\count3=7 \\showthe\\count\\n",
  "\\toks0={\\the\\count0}\\showthe\\toks0",
  "\\count0=2 \\showthe\\numexpr\\the\\count0+3\\relax",
  "\\toks0={\\def\\x{A}}\\showthe\\toks0",
  "\\count0=2 \\count2=9 \\message{before}\\showthe\\count\\number\\count0\\message{after}",
  "\\toks0={a}\\the\\toks0\\showthe\\toks0",
  "\\font\\f=cmr10 at 12pt \\showthe\\f",
  "\\showthe\\glueexpr 2pt plus 3pt\\relax",
  "\\def\\s{\\showthe\\count0}\\count0=4 \\s",
  "\\edef\\s{\\showthe\\count0}\\count0=4 \\s",
  "\\count0=1 {\\count0=4 \\showthe\\count0}\\showthe\\count0",
  "\\catcode`!=13 \\toks0={!}\\showthe\\toks0",
  "\\toks0={\\noexpand\\foo #1}\\showthe\\toks0"
]) check(source)^]

fn safe_digestion(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 if(d.ast!=null) d.ast==(parse(source,{type:"latex"})^) else d.issue=="latex-digest-unsupported"
}
[for(source in ["\\begin{document}A\\showthe\\count0 B\\end{document}","\\begin{document}A\\showthe\\toks0 B\\end{document}"]) safe_digestion(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_showthe_main.tex","text")^;
let resources={local:{'script_showthe_child.tex':{data:input(root ++ "script_showthe_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{source_name:root ++ "script_showthe_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;
let n=input(root ++ "script_showthe_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==slice(n.messages,len(baseline),len(n.messages))^,diagnostics_match:r.diagnostic_records==n.diagnostics}
