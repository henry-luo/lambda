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
  "\\ExplSyntaxOn\\int_compare:nTF{1<2}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{1<=1}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{1>=2}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{1!=2}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{2==2}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{1<2<3}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{1<2>3}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nT{1<2}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nF{1>2}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{2<1}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{2>1}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{2=2}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{2=1}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{1<=2}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{2<=1}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{2>=2}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{2>=1}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{1!=1}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{1==2}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nT{1>2}{Y}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nF{1<2}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{-3<-2}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{(2+3)*4>19}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{7/2=4}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{1<2<=2<3}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{1<2=2!=3}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{1<2>3<4}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{3>2>1}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{1<2<3<4<5}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\n{4}\\int_compare:nTF{\\n>3}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\lt{<}\\int_compare:nTF{1\\lt2}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\count0=5 \\int_compare:nTF{\\count0=5}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{\\int_eval:n{2+3}=5}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{\\int_abs:n{-4}>=4}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{\\c_zero_int<\\c_one_int}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\edef\\x{\\int_compare:nTF{2<3}{Y}{N}}\\x\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\cmp\\int_compare:nTF\\cmp{2<3}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\iftrue\\int_compare:nTF{2<3}{Y}{N}\\else X\\fi\\ExplSyntaxOff",
  "\\pdfsetrandomseed 123 \\ExplSyntaxOn\\int_compare:nTF{3<2<\\pdfuniformdeviate10}{Y}{N},\\pdfuniformdeviate10\\ExplSyntaxOff",
  "\\pdfsetrandomseed 123 \\ExplSyntaxOn\\int_compare:nTF{1<3<\\pdfuniformdeviate10}{Y}{N},\\pdfuniformdeviate10\\ExplSyntaxOff",
  "\\pdfsetrandomseed 123 \\ExplSyntaxOn\\int_compare:nTF{\\pdfuniformdeviate10<3<\\pdfuniformdeviate10}{Y}{N},\\pdfuniformdeviate10\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{`A>64}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\int_compare:nTF{\"10=16}{Y}{N}\\ExplSyntaxOff",
  "\\begin{document}A\\ExplSyntaxOn\\int_compare:nTF{1<2}{Y}{N}\\ExplSyntaxOff B\\end{document}"
]) check(source)^]

fn safe_digestion(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 if(d.ast!=null) d.ast==(parse(source,{type:"latex"})^) else d.issue=="latex-digest-unsupported"
}
[for(source in ["\\begin{document}\\ExplSyntaxOn\\int_compare:nTF{1<2}{Y}{N}\\ExplSyntaxOff\\end{document}","\\begin{document}$\\ExplSyntaxOn\\int_compare:nT{2>=1}{Y}\\ExplSyntaxOff$\\end{document}","\\ExplSyntaxOn\\edef\\x{\\int_compare:nTF{1<2<3}{Y}{N}}\\ExplSyntaxOff\\begin{document}\\x\\end{document}"]) safe_digestion(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_int_compare_expr_main.tex","text")^;
let resources={local:{'script_int_compare_expr_child.tex':{data:input(root ++ "script_int_compare_expr_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_int_compare_expr_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;
let n=input(root ++ "script_int_compare_expr_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==slice(n.messages,len(baseline),len(n.messages))^,diagnostics_match:r.diagnostic_records==n.diagnostics}
