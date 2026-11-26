// D10.2.2v2: value argument behavior was captured from TeX Live 2025.
import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true};
let preload=profile.preload_expl3_kernel(kernel,options,100000)^;
let prefix="\\ExplSyntaxOn\\def\\foo{EXPANDED}\\def\\pair:nn#1#2{[\\tl_to_str:n{#1}]/[\\tl_to_str:n{#2}]}";
let cases=[
 {name:"tokens",body:"\\tl_set:Nn\\l_tmpa_tl{B\\foo}\\exp_args:NnV\\pair:nn{\\foo}\\l_tmpa_tl",expected:"[\\foo ]/[B\\foo ]"},
 {name:"group",body:"\\tl_set:Nn\\l_tmpa_tl{A{B}~C}\\exp_args:NnV\\pair:nn{{X}~Y}\\l_tmpa_tl",expected:"[{X} Y]/[A{B} C]"},
 {name:"integer",body:"\\int_new:N\\l_test_value_int\\int_set:Nn\\l_test_value_int{-42}\\exp_args:NnV\\pair:nn{number}\\l_test_value_int",expected:"[number]/[-42]"},
 {name:"empty",body:"\\tl_clear:N\\l_tmpa_tl\\exp_args:NnV\\pair:nn{}\\l_tmpa_tl",expected:"[]/[]"},
 {name:"alias",body:"\\tl_set:Nn\\l_tmpa_tl{\\q_no_value}\\let\\test_apply\\exp_args:NnV\\test_apply\\pair:nn{Q}\\l_tmpa_tl",expected:"[Q]/[\\q_no_value ]"}];
[for(c in cases) (let r=profile.expand_with_kernel(prefix ++ c.body,kernel,options,100000,preload)^,
 {name:c.name,issue:r.issue,unsupported:r.unsupported,text:join([for(t in r.output)t.value],""),matches:join([for(t in r.output)t.value],"")==c.expected})]
let document_options={*:options,constructor_argument_mode:true};
let document_preload=profile.preload_expl3_kernel(kernel,document_options,100000)^;
let result=profile.expand_with_kernel(prefix ++ cases[0].body,kernel,document_options,100000,document_preload)^;
let tree=digest.digest(result)^;
{issue:tree.issue,unsupported:tree.unsupported}
