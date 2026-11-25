import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import compare: ._tex_script_recovery
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
[for(source in [
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\tl_set_eq:cN{l_b_tl}\\l_a_tl\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\tl_set_eq:cN\\tl_set_eq:cN Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\tl_set_eq:Nc\\l_b_tl{l_a_tl}\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\tl_set_eq:Nc\\tl_set_eq:Nc Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\tl_set_eq:cc{l_b_tl}{l_a_tl}\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\tl_set_eq:cc\\tl_set_eq:cc Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\tl_gset_eq:cN{l_b_tl}\\l_a_tl\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\tl_gset_eq:cN\\tl_gset_eq:cN Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\tl_gset_eq:Nc\\l_b_tl{l_a_tl}\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\tl_gset_eq:Nc\\tl_gset_eq:Nc Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\tl_gset_eq:cc{l_b_tl}{l_a_tl}\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\tl_gset_eq:cc\\tl_gset_eq:cc Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_a_seq\\seq_put_right:Nn\\l_a_seq{A}\\seq_set_eq:cN{l_b_seq}\\l_a_seq\\seq_use:Nn\\l_b_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\seq_set_eq:cN\\tl_set_eq:cN Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_a_seq\\seq_put_right:Nn\\l_a_seq{A}\\seq_set_eq:Nc\\l_b_seq{l_a_seq}\\seq_use:Nn\\l_b_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\seq_set_eq:Nc\\tl_set_eq:Nc Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\seq_new:N\\l_a_seq\\seq_put_right:Nn\\l_a_seq{A}\\seq_set_eq:cc{l_b_seq}{l_a_seq}\\seq_use:Nn\\l_b_seq{|}\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\seq_set_eq:cc\\tl_set_eq:cc Y\\else N\\fi\\ExplSyntaxOff"
]) compare.check_expansion(source,{script_expl3:true})^];
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_tl_set_eq_c_main.tex","text")^;
let resources={local:{'script_tl_set_eq_c_child.tex':{data:input(root ++ "script_tl_set_eq_c_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_tl_set_eq_c_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;let n=input(root ++ "script_tl_set_eq_c_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}
