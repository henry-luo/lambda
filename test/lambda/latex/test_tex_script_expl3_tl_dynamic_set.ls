// D10.2.2v2: dynamic token-list setters resolve names using TeX expansion.
import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true};
let preload=profile.preload_expl3_kernel(kernel,options,100000)^;
fn report(source) any^ {
    let r=profile.expand_with_kernel("\\ExplSyntaxOn" ++ source,kernel,options,100000,preload)^;
    {issue:r.issue,unsupported:r.unsupported,text:join([for(t in r.output)t.value],""),diagnostics:r.diagnostic_records}
}
// Name expansion, token preservation and local/global scope match TeX Live.
report("\\def\\foo{EXPANDED}\\def\\part{test}\\tl_set:cn{l_\\part _tl}{A\\foo}[\\tl_to_str:V\\l_test_tl]" ++
    "\\tl_set:cx{l_\\part _tl}{B\\foo}[\\tl_to_str:V\\l_test_tl]" ++
    "\\begingroup\\tl_set:cn{l_\\part _tl}{LOCAL}[\\tl_to_str:V\\l_test_tl]\\endgroup[\\tl_to_str:V\\l_test_tl]" ++
    "\\begingroup\\tl_gset:cn{l_\\part _tl}{G\\foo}\\endgroup[\\tl_to_str:V\\l_test_tl]" ++
    "\\begingroup\\tl_gset:cx{l_\\part _tl}{H\\foo}\\endgroup[\\tl_to_str:V\\l_test_tl]")^;
report("\\let\\assign\\tl_set:cn\\assign{l_test_tl}{VALUE}[\\tl_to_str:V\\l_test_tl]")^;
let r=profile.expand_with_kernel("\\ExplSyntaxOn\\tl_set:cn{l_test_tl}{X}",kernel,options,100000,preload)^;
let tree=digest.digest(r)^;
tree.ast == null and tree.issue == "latex-digest-unsupported" and tree.unsupported == "expl3 dynamic assignment source mapping"
