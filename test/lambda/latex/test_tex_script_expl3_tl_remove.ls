// D10.2.2v2: removal reuses the token-level replacement semantics.
import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true};
let preload=profile.preload_expl3_kernel(kernel,options,100000)^;
fn report(source) any^ {
    let r=profile.expand_with_kernel("\\ExplSyntaxOn" ++ source,kernel,options,100000,preload)^;
    {issue:r.issue,unsupported:r.unsupported,text:join([for(t in r.output)t.value],""),diagnostics:r.diagnostic_records}
}
// These token values and scope effects were compared with TeX Live.
report("\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{ab{ab}ab}" ++
    "\\tl_remove_once:Nn\\l_test_tl{ab}[\\tl_to_str:V\\l_test_tl]" ++
    "\\tl_remove_all:Nn\\l_test_tl{ab}[\\tl_to_str:V\\l_test_tl]")^;
report("\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{xxax}" ++
    "\\begingroup\\tl_remove_all:Nn\\l_test_tl{x}[\\tl_to_str:V\\l_test_tl]\\endgroup" ++
    "[\\tl_to_str:V\\l_test_tl]\\begingroup\\tl_gremove_once:Nn\\l_test_tl{x}\\endgroup" ++
    "[\\tl_to_str:V\\l_test_tl]\\begingroup\\tl_gremove_all:Nn\\l_test_tl{x}\\endgroup[\\tl_to_str:V\\l_test_tl]")^;
report("\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{abc}\\let\\remove\\tl_remove_once:Nn" ++
    "\\remove\\l_test_tl{b}[\\tl_to_str:V\\l_test_tl]")^;
report("\\tl_new:N\\l_test_tl\\tl_remove_all:Nn\\l_test_tl{}")^;
let r=profile.expand_with_kernel("\\ExplSyntaxOn\\tl_remove_all:Nn\\l_tmpa_tl{a}",kernel,options,100000,preload)^;
let tree=digest.digest(r)^;
tree.ast == null and tree.issue == "latex-digest-unsupported" and tree.unsupported == "expl3 token replacement source mapping"
