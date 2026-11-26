// D10.2.2v2: token-list trimming and meanings were captured from TeX Live 2025.
import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true};
let preload=profile.preload_expl3_kernel(kernel,options,100000)^;
let prefix="\\ExplSyntaxOn\\def\\foo{EXPANDED}";
let cases=[
 {name:"edges",body:"\\tl_set:Nx\\l_tmpa_tl{\\tl_trim_spaces:n{~~name~~}}\\tl_to_str:V\\l_tmpa_tl",expected:"name"},
 {name:"inside",body:"\\tl_set:Nx\\l_tmpa_tl{\\tl_trim_spaces:n{~~a~~b~~}}\\tl_to_str:V\\l_tmpa_tl",expected:"a b"},
 {name:"groups",body:"\\tl_set:Nx\\l_tmpa_tl{\\tl_trim_spaces:n{~{x~}~y~}}\\tl_to_str:V\\l_tmpa_tl",expected:"{x } y"},
 {name:"protected",body:"\\tl_set:Nx\\l_tmpa_tl{\\tl_trim_spaces:n{~\\foo~}}\\tl_to_str:V\\l_tmpa_tl",expected:"\\foo "},
 {name:"empty",body:"\\tl_set:Nx\\l_tmpa_tl{\\tl_trim_spaces:n{~~~}}\\tl_to_str:V\\l_tmpa_tl",expected:""},
 {name:"nested",body:"\\tl_set:Nx\\l_tmpa_tl{\\tl_trim_spaces:n{~{~x~}~}}\\tl_to_str:V\\l_tmpa_tl",expected:"{ x }"},
 {name:"unchanged",body:"\\tl_set:Nx\\l_tmpa_tl{\\tl_trim_spaces:n{abc}}\\tl_to_str:V\\l_tmpa_tl",expected:"abc"},
 {name:"value",body:"\\tl_set:Nn\\l_tmpa_tl{~abc~}\\tl_set:Nx\\l_tmpb_tl{\\tl_trim_spaces:V\\l_tmpa_tl}\\tl_to_str:V\\l_tmpb_tl",expected:"abc"},
 {name:"local",body:"\\tl_set:Nn\\l_tmpa_tl{~abc~}{\\tl_trim_spaces:N\\l_tmpa_tl\\tl_to_str:V\\l_tmpa_tl}\\tl_to_str:V\\l_tmpa_tl",expected:"{abc} abc "},
 {name:"global",body:"\\tl_set:Nn\\l_tmpa_tl{~abc~}{\\tl_gtrim_spaces:N\\l_tmpa_tl}\\tl_to_str:V\\l_tmpa_tl",expected:"{}abc"},
 {name:"cat12-space",body:"\\catcode32=12\\relax\\tl_set:Nx\\l_tmpa_tl{\\tl_trim_spaces:n{ x }}\\catcode32=9\\relax\\tl_to_str:V\\l_tmpa_tl",expected:" x "},
 {name:"meaning",body:"\\meaning\\tl_trim_spaces:n",expected:"\\long macro:#1->\\__tl_trim_spaces:nn {\\__tl_trim_mark: #1}{\\__kernel_exp_not:w \\exp_after:wN }"},
 {name:"meaning-aux",body:"\\meaning\\__tl_trim_spaces_auxi:w",expected:"\\long macro:#1\\__tl_trim_mark:  #2\\__tl_trim_mark: #3->#3\\__tl_trim_spaces_auxi:w \\__tl_trim_mark: #2\\__tl_trim_mark:  {#1}"}];
[for(c in cases) (let r=profile.expand_with_kernel(prefix ++ c.body,kernel,options,100000,preload)^,
 let text=join([for(t in r.output)t.value],""),
 {name:c.name,issue:r.issue,unsupported:r.unsupported,diagnostics:r.diagnostic_records,text:text,matches:text==c.expected and len(r.diagnostic_records)==0})]
let doc_options={*:options,constructor_argument_mode:true};
let doc_preload=profile.preload_expl3_kernel(kernel,doc_options,100000)^;
let doc=profile.expand_with_kernel(prefix ++ cases[0].body,kernel,doc_options,100000,doc_preload)^;
let tree=digest.digest(doc)^;
{issue:tree.issue,unsupported:tree.unsupported}
