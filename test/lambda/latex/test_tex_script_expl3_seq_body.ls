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
  "\\ExplSyntaxOn\\def\\s{\\s__seq}\\seq_if_empty:NTF\\s{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq}\\seq_count:N\\s\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n{a}\\__seq_item:n{b}}\\seq_use:Nn\\s{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n{a}\\__seq_item:n{b}}\\seq_item:Nn\\s{-1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n{a}}\\seq_put_right:Nn\\s{b}\\seq_use:Nn\\s{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n{a}\\__seq_item:n{b}}\\seq_reverse:N\\s\\seq_use:Nn\\s{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n{{a,b}}\\__seq_item:n{c}}\\seq_use:Nn\\s{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n{}\\__seq_item:n{b}}[\\seq_use:Nn\\s{|}]\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\long\\def\\s{\\s__seq\\__seq_item:n{a}\\__seq_item:n{b}}\\seq_use:Nn\\s{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\protected\\def\\s{\\s__seq\\__seq_item:n{a}}\\seq_use:Nn\\s{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n a\\__seq_item:n b}\\seq_use:Nn\\s{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_set:Nn\\s{\\s__seq\\__seq_item:n{a}\\__seq_item:n{b}}\\seq_count:N\\s\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\edef\\s{\\unexpanded{\\s__seq\\__seq_item:n{a}\\__seq_item:n{b}}}\\seq_item:Nn\\s{1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n{a}\\__seq_item:n{b}}\\seq_reverse:N\\s\\meaning\\s\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\long\\def\\s{\\s__seq\\__seq_item:n{a}\\__seq_item:n{b}}\\seq_reverse:N\\s\\meaning\\s\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n{a}\\__seq_item:n{b}}\\seq_put_right:Nn\\s{c}\\meaning\\s\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n{b}\\__seq_item:n{a}}\\seq_sort:Nn\\s{\\str_compare:nNnTF{#1}>{#2}{\\sort_return_swapped:}{\\sort_return_same:}}\\seq_use:Nn\\s{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n{a}\\__seq_item:n{b}}{\\seq_put_right:Nn\\s{c}\\seq_use:Nn\\s{|}}/\\seq_use:Nn\\s{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n{a}\\__seq_item:n{b}}{\\seq_greverse:N\\s}\\seq_use:Nn\\s{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n{a}\\__seq_item:n{b}}\\seq_item:Nn\\s{0}/\\seq_item:Nn\\s{-1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n{1}\\__seq_item:n{2}}\\int_eval:n{\\seq_item:Nn\\s{2\\relax}+1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n{a}\\__seq_item:n{b}}\\let\\t\\s\\seq_reverse:N\\s\\seq_use:Nn\\t{|}/\\seq_use:Nn\\s{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n{a}\\__seq_item:n{b}}\\seq_if_empty:NTF\\s{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\protected\\def\\s{\\s__seq\\__seq_item:n{a}\\__seq_item:n{b}}\\seq_use:Nn\\s{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\protected\\def\\s{\\s__seq\\__seq_item:n{a}\\__seq_item:n{b}}\\seq_count:N\\s\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\protected\\def\\s{\\s__seq\\__seq_item:n{a}\\__seq_item:n{b}}\\seq_item:Nn\\s{1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\protected\\def\\s{\\s__seq\\__seq_item:n{a}\\__seq_item:n{b}}\\edef\\x{\\seq_use:Nn\\s{|}}\\x\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\protected\\def\\s{\\s__seq\\__seq_item:n{a}\\__seq_item:n{b}}\\edef\\x{\\seq_count:N\\s}\\x\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\protected\\def\\s{\\s__seq\\__seq_item:n{a}\\__seq_item:n{b}}\\edef\\x{\\seq_item:Nn\\s{1}}\\x\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n a\\__seq_item:n b}\\seq_put_right:Nn\\s{c}\\meaning\\s\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n a\\__seq_item:n b}\\seq_put_left:Nn\\s{c}\\meaning\\s\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n a\\__seq_item:n b}\\seq_reverse:N\\s\\meaning\\s\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n a\\__seq_item:n b}\\seq_greverse:N\\s\\meaning\\s\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n a\\__seq_item:n b}\\seq_sort:Nn\\s{\\sort_return_same:}\\meaning\\s\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n{10}\\__seq_item:n{20}}\\int_eval:n{\\seq_item:Nn\\s{2\\relax}+1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n{{}}}\\seq_count:N\\s\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n a\\__seq_item:n b}\\seq_put_left:Nn\\s{{x,y}}\\seq_use:Nn\\s{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n a\\__seq_item:n b}{\\seq_gput_right:Nn\\s{c}}\\seq_use:Nn\\s{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n{a}\\__seq_item:n{b}}\\seq_item:Nn\\s{1+1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n{a}\\__seq_item:n{b}}\\seq_reverse:N\\s\\seq_item:Nn\\s{1}\\ExplSyntaxOff"
]) check(source)^]

fn safe_expansion(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 if(r.issue!=null) r.issue=="tex-unsupported-primitive" or r.issue=="expl3-invalid-sequence" else {
  let b=reconstruct.reconstruct(r)^;let n=parse(source,{type:"tex"})^;
  b.issue==null and b.text==n.text and r.diagnostic_records==n.diagnostics
 }
}
[for(source in ["\\ExplSyntaxOn\\protected\\def\\s{\\s__seq\\__seq_item:n{a}\\__seq_item:n{b}}\\seq_reverse:N\\s\\seq_use:Nn\\s{|}\\ExplSyntaxOff", "\\ExplSyntaxOn\\protected\\def\\s{\\s__seq\\__seq_item:n{a}\\__seq_item:n{b}}\\seq_put_right:Nn\\s{c}\\seq_use:Nn\\s{|}\\ExplSyntaxOff"]) safe_expansion(source)^]

fn safe_digestion(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 if(d.ast!=null) d.ast==(parse(source,{type:"latex"})^) else d.issue=="latex-digest-unsupported"
}
[for(source in ["\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n{a}\\__seq_item:n{b}}\\ExplSyntaxOff\\begin{document}\\ExplSyntaxOn\\seq_use:Nn\\s{|}\\ExplSyntaxOff\\end{document}","\\begin{document}\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n{a}}\\seq_item:Nn\\s{1}\\ExplSyntaxOff\\end{document}","\\ExplSyntaxOn\\def\\s{\\s__seq\\__seq_item:n a\\__seq_item:n b}\\seq_reverse:N\\s\\ExplSyntaxOff\\begin{document}Hello\\end{document}"]) safe_digestion(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_seq_body_main.tex","text")^;
let resources={local:{'script_seq_body_child.tex':{data:input(root ++ "script_seq_body_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_seq_body_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;let n=input(root ++ "script_seq_body_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==slice(n.messages,len(baseline),len(n.messages))^,diagnostics_match:r.diagnostic_records==n.diagnostics}
