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
  "\\ExplSyntaxOn\\meaning\\tl_new:c/\\meaning\\seq_new:c/\\meaning\\tl_clear:c/\\meaning\\tl_gclear:c/\\meaning\\seq_clear:c/\\meaning\\seq_gclear:c\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\show\\tl_new:c\\show\\seq_clear:c\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:c{l_test_tl}\\tl_use:N\\l_test_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:c{l_test_seq}\\seq_use:Nn\\l_test_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\prefix{l_test}\\tl_new:c{\\prefix _tl}\\tl_set:Nn\\l_test_tl{A}\\tl_use:N\\l_test_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\prefix{l_test}\\seq_new:c{\\prefix _seq}\\seq_put_right:Nn\\l_test_seq{A}\\seq_use:Nn\\l_test_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{A}\\tl_clear:c{l_test_tl}[\\tl_use:N\\l_test_tl]\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_test_seq\\seq_put_right:Nn\\l_test_seq{A}\\seq_clear:c{l_test_seq}[\\seq_use:Nn\\l_test_seq{|}]\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{A}{\\tl_clear:c{l_test_tl}[\\tl_use:N\\l_test_tl]}\\tl_use:N\\l_test_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_test_seq\\seq_put_right:Nn\\l_test_seq{A}{\\seq_clear:c{l_test_seq}[\\seq_use:Nn\\l_test_seq{|}]}\\seq_use:Nn\\l_test_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{A}{\\tl_gclear:c{l_test_tl}}[\\tl_use:N\\l_test_tl]\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_test_seq\\seq_put_right:Nn\\l_test_seq{A}{\\seq_gclear:c{l_test_seq}}[\\seq_use:Nn\\l_test_seq{|}]\\ExplSyntaxOff",
  "\\ExplSyntaxOn{\\tl_new:c{l_test_tl}}\\tl_set:Nn\\l_test_tl{A}\\tl_use:N\\l_test_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn{\\seq_new:c{l_test_seq}}\\seq_put_right:Nn\\l_test_seq{A}\\seq_use:Nn\\l_test_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{A}\\globaldefs=1 {\\tl_clear:c{l_test_tl}}\\globaldefs=0 [\\tl_use:N\\l_test_tl]\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_test_seq\\seq_put_right:Nn\\l_test_seq{A}\\globaldefs=-1 {\\seq_gclear:c{l_test_seq}}\\globaldefs=0 \\seq_use:Nn\\l_test_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{A}\\afterassignment X\\tl_clear:c{l_test_tl}[\\tl_use:N\\l_test_tl]\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_test_seq\\seq_put_right:Nn\\l_test_seq{A}\\afterassignment X\\seq_clear:c{l_test_seq}[\\seq_use:Nn\\l_test_seq{|}]\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\let\\q\\tl_new:c\\q{l_test_tl}\\tl_set:Nn\\l_test_tl{A}\\tl_use:N\\l_test_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\edef\\q{\\tl_new:c{l_test_tl}}\\q\\tl_set:Nn\\l_test_tl{A}\\tl_use:N\\l_test_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\globaldefs=-1 {\\tl_new:c{l_test_tl}}\\globaldefs=0 \\cs_if_exist:NTF\\l_test_tl{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\globaldefs=-1 {\\seq_new:c{l_test_seq}}\\globaldefs=0 \\cs_if_exist:NTF\\l_test_seq{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\expandafter\\let\\csname l_test_tl\\endcsname\\relax\\tl_new:c{l_test_tl}\\tl_set:Nn\\l_test_tl{A}\\tl_use:N\\l_test_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\expandafter\\let\\csname l_test_seq\\endcsname\\relax\\seq_new:c{l_test_seq}\\seq_put_right:Nn\\l_test_seq{A}\\seq_use:Nn\\l_test_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{藏α}\\tl_clear:c{l_test_tl}\\meaning\\l_test_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_test_seq\\seq_put_right:Nn\\l_test_seq{藏α}\\seq_clear:c{l_test_seq}\\meaning\\l_test_seq\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\prefix{l_test}\\tl_new:c{\\prefix _tl}\\tl_if_exist:cTF{\\prefix _tl}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\def\\prefix{l_test}\\seq_new:c{\\prefix _seq}\\seq_if_exist:cTF{\\prefix _seq}{Y}{N}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{A}\\let\\q\\tl_gclear:c{\\q{l_test_tl}}[\\tl_use:N\\l_test_tl]\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_test_seq\\seq_put_right:Nn\\l_test_seq{A}\\let\\q\\seq_gclear:c{\\q{l_test_seq}}[\\seq_use:Nn\\l_test_seq{|}]\\ExplSyntaxOff"
]) check(source)^]

fn guarded(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let d=digest.digest(r)^;
 d.ast==null and d.issue=="latex-digest-unsupported" and d.unsupported=="expl3 dynamic assignment source mapping"
}
[for(source in [
 "\\ExplSyntaxOn\\tl_new:c{l_test_tl}\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\seq_new:c{l_test_seq}\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\tl_clear:c{l_tmpa_tl}\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
 "\\ExplSyntaxOn\\let\\q\\tl_new:c\\q{l_test_tl}\\ExplSyntaxOff\\begin{document}Hello\\end{document}"
]) guarded(source)^]
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_decl_c_main.tex","text")^;
let resources={local:{'script_decl_c_child.tex':{data:input(root ++ "script_decl_c_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_decl_c_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;let n=input(root ++ "script_decl_c_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}
