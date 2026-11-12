import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let baseline=parse("",{type:"tex"})^.messages;
fn check(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let n=parse(source,{type:"tex"})^;let b=reconstruct.reconstruct(r)^;
 {issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==slice(n.messages,len(baseline),len(n.messages))^,diagnostics_match:r.diagnostic_records==n.diagnostics}
}
[for (source in [
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_item:Nn\\l_s_seq{1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_item:Nn\\l_s_seq{2}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_item:Nn\\l_s_seq{3}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}[\\seq_item:Nn\\l_s_seq{0}]\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}[\\seq_item:Nn\\l_s_seq{4}]\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_item:Nn\\l_s_seq{-1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_item:Nn\\l_s_seq{-2}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_item:Nn\\l_s_seq{-3}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}[\\seq_item:Nn\\l_s_seq{-4}]\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_item:Nn\\l_s_seq{1+1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_item:Nn\\l_s_seq{3-4}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_item:Nn\\l_s_seq{4/2}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_item:Nn\\l_s_seq{\\int_eval:n{1+1}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\def\\i{2}\\seq_item:Nn\\l_s_seq{\\i}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_item:Nn\\l_s_seq{\\numexpr1+1\\relax}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_item:Nn\\l_s_seq{2\\relax}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_item:Nn\\l_s_seq{\\int_abs:n{-2}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_reverse:N\\l_s_seq\\seq_item:Nn\\l_s_seq{1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\edef\\x{\\seq_item:Nn\\l_s_seq{2}}\\x\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq[\\seq_item:Nn\\l_s_seq{1}]\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,{b,c},d}\\seq_item:Nn\\l_s_seq{2}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{}\\seq_put_right:Nn\\l_s_seq{X}[\\seq_item:Nn\\l_s_seq{1}]/\\seq_item:Nn\\l_s_seq{2}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\def\\word{A}\\seq_put_right:Nn\\l_s_seq{\\word}\\edef\\saved{\\seq_item:Nn\\l_s_seq{1}}\\def\\word{B}\\saved\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\let\\x\\l_s_seq\\seq_gclear:N\\l_s_seq\\seq_item:Nn\\x{-1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_item:Nn\\l_s_seq{\\pdfuniformdeviate3+1},\\pdfuniformdeviate10\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{10,20,30}\\int_eval:n{\\seq_item:Nn\\l_s_seq{2\\relax}+1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{10,20,30}\\int_eval:n{\\seq_item:Nn\\l_s_seq{\\numexpr1+1\\relax}+1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{10,20,30}\\int_compare:nTF{\\seq_item:Nn\\l_s_seq{2\\relax}=20}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{10,20,30}\\int_compare:nNnTF{\\seq_item:Nn\\l_s_seq{2\\relax}}={20}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{10,20,30}\\int_if_even:nTF{\\seq_item:Nn\\l_s_seq{2\\relax}}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{10,20,30}\\int_max:nn{\\seq_item:Nn\\l_s_seq{2\\relax}}{1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{10,20,30}\\seq_item:Nn\\l_s_seq{\\seq_item:Nn\\l_s_seq{1}-9}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{10,20,30}\\seq_item:Nn\\l_s_seq{1+\\int_eval:n{1}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{10,20,30}\\edef\\x{\\seq_item:Nn\\l_s_seq{2\\relax}}\\x\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{10,20,30}\\seq_item:Nn\\l_s_seq{0}\\seq_item:Nn\\l_s_seq{-1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{10,20,30}\\seq_item:Nn\\l_s_seq{2}\\seq_item:Nn\\l_s_seq{1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{10,20,30}\\seq_item:Nn\\l_s_seq{\\int_compare:nTF{1<2}{2}{3}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{\\int_eval:n{1+2}}\\seq_item:Nn\\l_s_seq{1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{10,20,30}\\def\\i#1{#1+1}\\seq_item:Nn\\l_s_seq{\\i{1}}X\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{10,20,30}\\def\\i#1{2}\\seq_item:Nn\\l_s_seq{\\i}X\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{10,20,30}\\def\\i#1{2}\\seq_item:Nn\\l_s_seq{\\i{}}X\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{10,20,30}\\def\\i#1{#1+1}\\seq_item:Nn\\l_s_seq{\\i1}X\\ExplSyntaxOff"
]) check(source)^]

fn safe_expansion(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 if(r.issue!=null) r.issue=="tex-unsupported-primitive" else {
  let b=reconstruct.reconstruct(r)^;let n=parse(source,{type:"tex"})^;
  b.issue==null and b.text==n.text and r.diagnostic_records==n.diagnostics
 }
}
[for(source in ["\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{10,20,30}\\seq_item:Nn\\l_s_seq{2\\relax+1}X\\ExplSyntaxOff","\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{10,20,30}\\def\\stop{\\relax}\\seq_item:Nn\\l_s_seq{2\\stop+1}X\\ExplSyntaxOff"]) safe_expansion(source)^]
fn safe_digestion(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 if(d.ast!=null) d.ast==(parse(source,{type:"latex"})^) else d.issue=="latex-digest-unsupported"
}
[for(source in ["\\begin{document}\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b}\\seq_item:Nn\\l_s_seq{2}\\ExplSyntaxOff\\end{document}","\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b}\\edef\\x{\\seq_item:Nn\\l_s_seq{2}}\\ExplSyntaxOff\\begin{document}\\x\\end{document}","\\begin{document}$\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b}\\seq_item:Nn\\l_s_seq{2}\\ExplSyntaxOff$\\end{document}"]) safe_digestion(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_seq_item_main.tex","text")^;
let resources={local:{'script_seq_item_child.tex':{data:input(root ++ "script_seq_item_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_seq_item_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;let n=input(root ++ "script_seq_item_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==slice(n.messages,len(baseline),len(n.messages))^,diagnostics_match:r.diagnostic_records==n.diagnostics}
