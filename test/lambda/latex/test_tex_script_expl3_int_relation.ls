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
  "\\ExplSyntaxOn\\def\\rel{<}\\int_compare:nNnTF{1}\\rel{2}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\rel{>}\\int_compare:nNnTF{1}\\rel{2}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\rel{=}\\int_compare:nNnTF{2}\\rel{2}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\rel{<}\\int_compare:nNnT{1}\\rel{2}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\rel{>}\\int_compare:nNnF{1}\\rel{2}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\lt{<}\\def\\rel{\\lt}\\int_compare:nNnTF{1}\\rel{2}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\lt{<}\\let\\rel\\lt\\int_compare:nNnTF{1}\\rel{2}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\rel{<}\\edef\\x{\\int_compare:nNnTF{1}\\rel{2}{Y}{N}}\\x\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\rel{<}\\int_compare:nNnTF{\\int_abs:n{-1}}\\rel{2}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\rel{<}\\int_compare:nNnTF{1}\\rel{\\int_max:nn{2}{3}}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\rel{<}{\\def\\rel{>}\\int_compare:nNnTF{1}\\rel{2}{Y}{N}}\\int_compare:nNnTF{1}\\rel{2}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\rel{<}\\int_compare:nNnTF{1}\\rel{2}{\\int_compare:nNnTF{3}\\rel{4}{A}{B}}{N}\\ExplSyntaxOff",
  "\\pdfsetrandomseed 123 \\ExplSyntaxOn\\def\\rel{<}\\int_compare:nNnTF{\\pdfuniformdeviate10}\\rel{3}{Y}{N},\\pdfuniformdeviate10\\ExplSyntaxOff",
  "\\begin{document}\\ExplSyntaxOn\\def\\rel{<}\\int_compare:nNnTF{1}\\rel{2}{Y}{N}\\ExplSyntaxOff\\end{document}",
  "\\ExplSyntaxOn\\def\\rel{<}\\int_compare:nNnTF{1}\\rel{2}{A}{B}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\rel{>}\\int_compare:nNnTF{3}\\rel{2}{A}{B}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\rel{=}\\int_compare:nNnTF{2}\\rel{3}{A}{B}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\rel{~<~}\\int_compare:nNnTF{1}\\rel{2}{A}{B}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\a{<}\\def\\b{\\a}\\def\\rel{\\b}\\int_compare:nNnTF{1}\\rel{2}{A}{B}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\rel{<}\\int_compare:nNnTF{\\c_zero_int}\\rel{\\c_one_int}{A}{B}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\rel{<}\\int_compare:nNnTF{1\\relax}\\rel{2}{A}{B}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\rel{<}\\int_compare:nNnTF{1}\\rel{2\\relax}{A}{B}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\rel{<}\\int_compare:nNnTF{\\numexpr1+1\\relax}\\rel{3}{A}{B}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\rel{<}\\int_compare:nNnTF{1}\\rel{\\numexpr1+1\\relax}{A}{B}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\rel{<}\\def\\x{5}\\int_compare:nNnTF{\\x}\\rel{9}{A}{B}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\rel{<}\\int_compare:nNnT{2}\\rel{1}{A}X\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\rel{<}\\int_compare:nNnF{2}\\rel{1}{B}X\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\rel{<}\\int_compare:nNnTF{1}\\rel{2}{}{N}X\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\rel{<}\\int_compare:nNnTF{2}\\rel{1}{Y}{}X\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\rel{<}\\int_compare:nTF{1<2}{\\int_compare:nNnTF{1}\\rel{2}{Y}{N}}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\rel{\\int_compare:nTF{1<2}{<}{>}}\\int_compare:nNnTF{1}\\rel{2}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\rel{\\int_compare:nNnTF{1}<{2}{<}{>}}\\int_compare:nNnTF{1}\\rel{2}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\rel{\\int_compare:nTF{1>2}{>}{<}}\\int_compare:nNnTF{1}\\rel{2}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\rel{<}\\int_compare:nNnTF{\\int_compare:nTF{1<2}{1}{2}}\\rel{2}{Y}{N}\\ExplSyntaxOff"
]) check(source)^]

fn safe_expansion(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 if(r.issue!=null) r.issue=="tex-unsupported-primitive" else {
  let b=reconstruct.reconstruct(r)^;let n=parse(source,{type:"tex"})^;
  b.issue==null and b.text==n.text and r.diagnostic_records==n.diagnostics
 }
}
[for(source in ["\\ExplSyntaxOn\\def\\rel{\\ifnum1<2\\relax<\\else>\\fi}\\int_compare:nNnTF{1}\\rel{2}{Y}{N}\\ExplSyntaxOff", "\\ExplSyntaxOn\\def\\eat#1{<}\\int_compare:nNnTF{1}\\eat{2}{Y}{N}X\\ExplSyntaxOff", "\\ExplSyntaxOn\\let\\rel=<\\int_compare:nNnTF{1}\\rel{2}{Y}{N}\\ExplSyntaxOff"]) safe_expansion(source)^]
fn safe_digestion(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 if(d.ast!=null) d.ast==(parse(source,{type:"latex"})^) else d.issue=="latex-digest-unsupported"
}
[for(source in ["\\begin{document}\\ExplSyntaxOn\\def\\rel{<}\\int_compare:nNnTF{1}\\rel{2}{Y}{N}\\ExplSyntaxOff\\end{document}","\\begin{document}$\\ExplSyntaxOn\\def\\rel{<}\\int_compare:nNnT{1}\\rel{2}{Y}\\ExplSyntaxOff$\\end{document}","\\ExplSyntaxOn\\def\\rel{<}\\edef\\x{\\int_compare:nNnTF{1}\\rel{2}{Y}{N}}\\ExplSyntaxOff\\begin{document}\\x\\end{document}"]) safe_digestion(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_int_relation_main.tex","text")^;
let resources={local:{'script_int_relation_child.tex':{data:input(root ++ "script_int_relation_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_int_relation_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;
let n=input(root ++ "script_int_relation_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==slice(n.messages,len(baseline),len(n.messages))^,diagnostics_match:r.diagnostic_records==n.diagnostics}
