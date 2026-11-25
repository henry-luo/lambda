import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import compare: ._tex_script_recovery
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
[for(source in [
  "\\ExplSyntaxOn\\meaning\\tl_set_eq:NN/\\meaning\\tl_gset_eq:NN/\\meaning\\seq_set_eq:NN/\\meaning\\seq_gset_eq:NN\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\show\\tl_set_eq:NN\\show\\tl_gset_eq:NN\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\ifx\\tl_set_eq:NN\\seq_set_eq:NN Y\\else N\\fi/\\ifx\\tl_gset_eq:NN\\seq_gset_eq:NN Y\\else N\\fi\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_new:N\\l_b_tl\\tl_set:Nn\\l_a_tl{A}\\tl_set_eq:NN\\l_b_tl\\l_a_tl\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_new:N\\l_b_tl\\tl_set:Nn\\l_a_tl{A}\\tl_set_eq:NN\\l_b_tl\\l_a_tl\\tl_set:Nn\\l_a_tl{B}\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_new:N\\l_b_tl\\tl_set:Nn\\l_a_tl{A}{\\tl_set_eq:NN\\l_b_tl\\l_a_tl\\tl_use:N\\l_b_tl}[\\tl_use:N\\l_b_tl]\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_new:N\\l_b_tl\\tl_set:Nn\\l_a_tl{A}{\\tl_gset_eq:NN\\l_b_tl\\l_a_tl}\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_new:N\\l_b_tl\\tl_set:Nn\\l_a_tl{A}\\globaldefs=1 {\\tl_set_eq:NN\\l_b_tl\\l_a_tl}\\globaldefs=0 \\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_new:N\\l_b_tl\\tl_set:Nn\\l_a_tl{A}\\globaldefs=-1 {\\tl_gset_eq:NN\\l_b_tl\\l_a_tl}\\globaldefs=0 [\\tl_use:N\\l_b_tl]\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_new:N\\l_b_tl\\tl_set:Nn\\l_a_tl{A}\\afterassignment X\\tl_set_eq:NN\\l_b_tl\\l_a_tl\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_new:N\\l_b_tl\\tl_set:Nn\\l_a_tl{A}\\afterassignment X\\tl_gset_eq:NN\\l_b_tl\\l_a_tl\\tl_use:N\\l_b_tl\\ExplSyntaxOff",
  "\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_new:N\\l_b_tl\\tl_set:Nn\\l_a_tl{A}\\let\\f\\tl_set_eq:NN\\f\\l_b_tl\\l_a_tl\\tl_use:N\\l_b_tl\\ExplSyntaxOff"
]) compare.check_expansion(source,{script_expl3:true})^];
let root="test/lambda/latex/fixtures/local_packages/";
let source=input(root ++ "script_tl_set_eq_main.tex","text")^;
let resources={local:{'script_tl_set_eq_child.tex':{data:input(root ++ "script_tl_set_eq_child.tex","text")^}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,source_name:root ++ "script_tl_set_eq_main.tex",echo_definitions:true,adapters:[],resources:resources},100000)^;
let b=reconstruct.reconstruct(r)^;let n=input(root ++ "script_tl_set_eq_main.tex","tex")^;
{issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,messages_match:r.messages==n.messages,diagnostics_match:r.diagnostic_records==n.diagnostics}
