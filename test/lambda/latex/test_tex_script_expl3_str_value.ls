// D10.2.2v2: expected values and macro meanings captured from TeX Live 2025.
import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true};
let preload=profile.preload_expl3_kernel(kernel,options,100000)^;
let prefix="\\ExplSyntaxOn\\def\\foo{EXPANDED}";
let cases=[
 {name:"equal",body:"\\tl_set:Nn\\l_tmpa_tl{abc}\\str_if_eq:VnTF\\l_tmpa_tl{abc}{T}{F}",expected:"T"},
 {name:"unequal",body:"\\tl_set:Nn\\l_tmpa_tl{abc}\\str_if_eq:VnTF\\l_tmpa_tl{abd}{T}{F}",expected:"F"},
 {name:"true",body:"\\tl_set:Nn\\l_tmpa_tl{abc}\\str_if_eq:VnT\\l_tmpa_tl{abc}{T}",expected:"T"},
 {name:"false",body:"\\tl_set:Nn\\l_tmpa_tl{abc}\\str_if_eq:VnF\\l_tmpa_tl{abd}{T}",expected:"T"},
 {name:"control",body:"\\tl_set:Nn\\l_tmpa_tl{\\foo}\\str_if_eq:VnTF\\l_tmpa_tl{\\foo}{T}{F}",expected:"T"},
 {name:"control-expanded",body:"\\tl_set:Nn\\l_tmpa_tl{\\foo}\\str_if_eq:VnTF\\l_tmpa_tl{EXPANDED}{T}{F}",expected:"F"},
 {name:"spaces",body:"\\tl_set:Nn\\l_tmpa_tl{a~b}\\str_if_eq:VnTF\\l_tmpa_tl{a~b}{T}{F}",expected:"T"},
 {name:"empty",body:"\\tl_clear:N\\l_tmpa_tl\\str_if_eq:VnTF\\l_tmpa_tl{}{T}{F}",expected:"T"},
 {name:"integer",body:"\\int_new:N\\l_test_int\\int_set:Nn\\l_test_int{-42}\\str_if_eq:VnTF\\l_test_int{-42}{T}{F}",expected:"T"},
 {name:"meaning",body:"\\meaning\\str_if_eq:VnTF",expected:"\\long macro:->\\exp_args:NV \\str_if_eq:nnTF "}];
[for(c in cases) (let r=profile.expand_with_kernel(prefix ++ c.body,kernel,options,100000,preload)^,
 let text=join([for(t in r.output)t.value],""),
 {name:c.name,issue:r.issue,unsupported:r.unsupported,text:text,matches:text==c.expected})]
let document_options={*:options,constructor_argument_mode:true};
let document_preload=profile.preload_expl3_kernel(kernel,document_options,100000)^;
let document=profile.expand_with_kernel(prefix ++ cases[0].body,kernel,document_options,100000,document_preload)^;
let tree=digest.digest(document)^;
{issue:tree.issue,unsupported:tree.unsupported}
