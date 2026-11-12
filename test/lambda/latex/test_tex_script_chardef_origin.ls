import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let baseline=parse("",{type:"tex"})^.messages;
fn check(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let n=parse(source,{type:"tex"})^;
 let b=reconstruct.reconstruct(r)^;
 {issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==slice(n.messages,len(baseline),len(n.messages))^,diagnostics_match:r.diagnostic_records==n.diagnostics}
}
[for (source in [
  "\\chardef\\z=65 \\z",
  "\\chardef\\z=36 A\\z B",
  "\\chardef\\z=35 \\begin{document}A\\z B\\end{document}",
  "\\chardef\\z=65 \\def\\x{\\z}\\begin{document}\\x B\\end{document}",
  "\\chardef\\z=233 \\begin{document}A\\z B\\end{document}",
  "\\chardef\\z=0 \\z",
  "\\chardef\\z=1 \\z",
  "\\chardef\\z=32 A\\z B",
  "\\chardef\\z=37 A\\z B",
  "\\chardef\\z=38 A\\z B",
  "\\chardef\\z=92 A\\z B",
  "\\chardef\\z=94 A\\z B",
  "\\chardef\\z=95 A\\z B",
  "\\chardef\\z=123 A\\z B",
  "\\chardef\\z=125 A\\z B",
  "\\chardef\\z=126 A\\z B",
  "\\chardef\\z=10 A\\z B",
  "\\chardef\\z=65 \\let\\x\\z A\\x B",
  "\\chardef\\z=65 {\\chardef\\z=66 \\z}\\z",
  "\\chardef\\z=65 \\iftrue\\z\\else X\\fi",
  "\\chardef\\z=65 \\edef\\x{\\noexpand\\z}\\x",
  "\\chardef\\z=65 \\def\\x{\\z\\z}\\x",
  "\\chardef\\z=65 \\begin{document}$x\\z y$\\end{document}",
  "\\ExplSyntaxOn\\c_zero_int\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\x{\\c_zero_int}\\x\\ExplSyntaxOff"
]) check(source)^]

fn safe_digestion(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 if(d.ast!=null) d.ast==(parse(source,{type:"latex"})^) else d.issue=="latex-digest-unsupported"
}
[for(source in ["\\chardef\\z=65 \\begin{document}A\\z B\\end{document}","\\chardef\\z=36 \\begin{document}A\\z B\\end{document}","\\begin{document}\\ExplSyntaxOn A\\c_zero_int B\\ExplSyntaxOff\\end{document}"]) safe_digestion(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_chardef_main.tex","text")^;
let resources={local:{'script_chardef_child.tex':{data:input(root ++ "script_chardef_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_chardef_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;
let n=input(root ++ "script_chardef_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==slice(n.messages,len(baseline),len(n.messages))^,diagnostics_match:r.diagnostic_records==n.diagnostics}

fn exact_digestion(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 d.ast!=null and d.ast==(parse(source,{type:"latex"})^)
}
[for(source in ["\\mathchardef\\foo=1 \\begin{document}A\\foo B\\end{document}","\\mathchardef\\alpha=1 \\begin{document}A\\alpha B\\end{document}"]) exact_digestion(source)^]
