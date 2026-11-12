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
[for (source in ["\\show\\foo", "\\def\\foo{ABC}\\show\\foo", "\\show A", "\\show\\relax", "\\showbox3", "\\showlists A\\showgroups B\\showifs C", "\\long\\def\\foo#1{Hi#1}\\show\\foo", "\\protected\\def\\foo{ABC}\\show\\foo", "\\let\\alias=\\relax\\show\\alias", "\\show\\show", "\\show1 B", "\\show$", "\\show\\undefined", "\\countdef\\n=12 \\show\\n", "\\dimendef\\d=3 \\show\\d", "\\skipdef\\s=5 \\show\\s", "\\muskipdef\\m=7 \\show\\m", "\\toksdef\\t=9 \\show\\t", "\\chardef\\x=65 \\show\\x", "\\font\\f=cmr10 \\show\\f", "\\message{before}\\show\\relax\\message{after}", "\\message{before}\\showbox0\\message{after}", "\\showbox\\numexpr2+3\\relax X", "\\let\\inspect=\\show \\inspect\\relax", "\\def\\foo{A}\\def\\bar{\\show\\foo}\\bar", "\\iftrue\\show\\foo\\else X\\fi", "\\escapechar=-1 \\show\\foo", "\\escapechar=33 \\show\\foo", "\\catcode`\\~=13 \\def~{abc}\\show~", "\\show", "\\showbox255", "\\dimendef\\d=3 \\meaning\\d", "\\skipdef\\s=5 \\meaning\\s", "\\muskipdef\\m=7 \\meaning\\m", "\\showlists\\showgroups\\showifs", "\\begin{document}A\\show\\foo B\\end{document}", "\\show\\section", "\\show\\textbf", "\\show\\begin", "\\show\\end", "\\escapechar=33 \\showbox3", "\\show\\csname", "\\show\\par", "\\show\\ ", "\\show\\%", "\\meaning\\section", "\\meaning\\%", "\\expandafter\\show\\csname\\endcsname", "\\let\\foo=\\section \\show\\foo", "\\escapechar=-1 \\show\\section"]) check(source)^]

fn safe_digestion(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 if(d.ast!=null) d.ast==(parse(source,{type:"latex"})^) else d.issue=="latex-digest-unsupported"
}
[for(source in ["\\begin{document}A\\show\\foo B\\end{document}","\\begin{document}A\\showbox0 B\\end{document}","\\begin{document}A\\showlists B\\end{document}"]) safe_digestion(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_show_main.tex","text")^;
let resources={local:{'script_show_child.tex':{data:input(root ++ "script_show_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{source_name:root ++ "script_show_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;
let n=input(root ++ "script_show_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==slice(n.messages,len(baseline),len(n.messages))^,diagnostics_match:r.diagnostic_records==n.diagnostics}
