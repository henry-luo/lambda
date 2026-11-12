import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
fn check(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let n=parse(source,{type:"tex"})^;let b=reconstruct.reconstruct(r)^;
 {issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}
}
[for(source in [
  "\\ExplSyntaxOn\\meaning\\tl_use:c/\\meaning\\seq_use:cn/\\meaning\\seq_item:cn/\\meaning\\seq_count:c/\\meaning\\tl_count:c\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\show\\tl_use:c\\show\\seq_item:cn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{abc}\\tl_use:c{l_test_tl}/\\tl_count:c{l_test_tl}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_test_seq\\seq_set_from_clist:Nn\\l_test_seq{a,b,c}\\seq_use:cn{l_test_seq}{|}/\\seq_count:c{l_test_seq}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_test_seq\\seq_set_from_clist:Nn\\l_test_seq{a,b,c}\\seq_item:cn{l_test_seq}{1}/\\seq_item:cn{l_test_seq}{-1}/\\seq_item:cn{l_test_seq}{5}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\prefix{l_test}\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{A}\\tl_use:c{\\prefix _tl}/\\tl_count:c{\\prefix _tl}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\prefix{l_test}\\seq_new:N\\l_test_seq\\seq_set_from_clist:Nn\\l_test_seq{a,b}\\seq_use:cn{\\prefix _seq}{|}/\\seq_item:cn{\\prefix _seq}{2}/\\seq_count:c{\\prefix _seq}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{{a}{b}}\\tl_count:c{l_test_tl}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_test_seq\\seq_set_from_clist:Nn\\l_test_seq{a,{b,c},d}\\seq_item:cn{l_test_seq}{2}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{A}{\\tl_set:Nn\\l_test_tl{B}\\tl_use:c{l_test_tl}}\\tl_use:c{l_test_tl}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_test_seq\\seq_put_right:Nn\\l_test_seq{A}{\\seq_put_right:Nn\\l_test_seq{B}\\seq_count:c{l_test_seq}}\\seq_count:c{l_test_seq}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{A}\\edef\\z{\\tl_use:c{l_test_tl}}\\meaning\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_test_seq\\seq_set_from_clist:Nn\\l_test_seq{a,b}\\edef\\z{\\seq_item:cn{l_test_seq}{2}}\\meaning\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{A}\\let\\q\\tl_use:c\\q{l_test_tl}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_test_seq\\seq_set_from_clist:Nn\\l_test_seq{a,b,c}\\int_eval:n{\\seq_count:c{l_test_seq}+\\tl_count:c{l_tmpa_tl}}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{藏α}\\tl_use:c{l_test_tl}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_test_seq\\seq_put_right:Nn\\l_test_seq{藏α}\\seq_use:cn{l_test_seq}{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_item:cn{l_s_seq}{1+1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b,c}\\seq_item:cn{l_s_seq}{-4}/\\seq_item:cn{l_s_seq}{0}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{}\\seq_use:cn{l_s_seq}{|}/\\seq_count:c{l_s_seq}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\def\\a{A}\\seq_put_right:Nn\\l_s_seq{\\a}\\def\\a{B}\\seq_item:cn{l_s_seq}{1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\tl_set:Nn\\l_s_tl{A}\\def\\prefix{l_s}\\let\\q\\tl_use:c\\q{\\prefix _tl}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\tl_set:Nn\\l_s_tl{A}\\afterassignment X\\tl_use:c{l_s_tl}\\def\\z{B}\\z\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\tl_set:Nn\\l_s_tl{~}\\tl_count:c{l_s_tl}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{{a}{b}}\\seq_item:cn{l_s_seq}{1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{a}\\let\\q\\seq_use:cn\\q{l_s_seq}{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\tl_set:Nn\\l_s_tl{A}\\cs_if_exist:NTF\\tl_use:c{Y}{N}/\\tl_use:c{l_s_tl}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_set_from_clist:Nn\\l_s_seq{a,b}\\int_eval:n{\\seq_count:c{l_s_seq}+1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\tl_set:Nn\\l_s_tl{abc}\\int_eval:n{\\tl_count:c{l_s_tl}+1}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\tl_set:Nn\\l_s_tl{A}\\let\\q\\tl_count:c\\q{l_s_tl}\\ExplSyntaxOff"
]) check(source)^]

fn guarded(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 d.ast==null and d.issue=="latex-digest-unsupported" and d.unsupported=="expl3 dynamic read source mapping"
}
[for(source in [
 "\\ExplSyntaxOn\\tl_use:c{l_tmpa_tl}\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_use:cn{l_s_seq}{|}\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_item:cn{l_s_seq}{1}\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_count:c{l_s_seq}\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\let\\q\\tl_count:c\\q{l_tmpa_tl}\\ExplSyntaxOff\\begin{document}Hello\\end{document}"
]) guarded(source)^]
fn rejected(source, issue) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 r.issue==issue
}
[rejected("\\ExplSyntaxOn\\tl_use:c{missing}\\ExplSyntaxOff", "expl3-undefined-variable"),
 rejected("\\ExplSyntaxOn\\tl_use:N\\scan_stop:\\ExplSyntaxOff", "expl3-undefined-variable"),
 rejected("\\ExplSyntaxOn\\tl_count:c{missing}\\ExplSyntaxOff", "tex-unsupported-primitive"),
 rejected("\\ExplSyntaxOn\\seq_count:c{missing}\\ExplSyntaxOff", "expl3-invalid-sequence")]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_use_c_main.tex","text")^;
let resources={local:{'script_use_c_child.tex':{data:input(root ++ "script_use_c_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_use_c_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;let n=input(root ++ "script_use_c_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}
