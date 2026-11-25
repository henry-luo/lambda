import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import compare: ._tex_script_recovery
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
[for(source in [
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\tl_set:Nn\\l_s_tl{A}\\tl_put_left:cn{l_s_tl}{B}\\tl_use:N\\l_s_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\tl_put_left:cn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\tl_set:Nn\\l_s_tl{A}{\\tl_put_left:cn{l_s_tl}{B}}\\tl_use:N\\l_s_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\tl_set:Nn\\l_s_tl{A}\\tl_put_right:cn{l_s_tl}{B}\\tl_use:N\\l_s_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\tl_put_right:cn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\tl_set:Nn\\l_s_tl{A}{\\tl_put_right:cn{l_s_tl}{B}}\\tl_use:N\\l_s_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\tl_set:Nn\\l_s_tl{A}\\tl_gput_left:cn{l_s_tl}{B}\\tl_use:N\\l_s_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\tl_gput_left:cn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\tl_set:Nn\\l_s_tl{A}{\\tl_gput_left:cn{l_s_tl}{B}}\\tl_use:N\\l_s_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\tl_set:Nn\\l_s_tl{A}\\tl_gput_right:cn{l_s_tl}{B}\\tl_use:N\\l_s_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\tl_gput_right:cn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_s_tl\\tl_set:Nn\\l_s_tl{A}{\\tl_gput_right:cn{l_s_tl}{B}}\\tl_use:N\\l_s_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\seq_put_left:cn{l_s_seq}{B}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\seq_put_left:cn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}{\\seq_put_left:cn{l_s_seq}{B}}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}\\seq_put_right:cn{l_s_seq}{B}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\meaning\\seq_put_right:cn\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\seq_put_right:Nn\\l_s_seq{A}{\\seq_put_right:cn{l_s_seq}{B}}\\seq_use:Nn\\l_s_seq{|}\\ExplSyntaxOff"
]) compare.check_expansion(source,{script_expl3:true})^];
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_put_cn_main.tex","text")^;
let resources={local:{'script_put_cn_child.tex':{data:input(root ++ "script_put_cn_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_put_cn_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;let n=input(root ++ "script_put_cn_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}
