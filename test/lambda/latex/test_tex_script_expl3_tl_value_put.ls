// D10.2.2v2: value insertion follows native token and register expansion.
import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true};
let preload=profile.preload_expl3_kernel(kernel,options,100000)^;
fn report(source) any^ {
    let r=profile.expand_with_kernel("\\ExplSyntaxOn" ++ source,kernel,options,100000,preload)^;
    {issue:r.issue,unsupported:r.unsupported,text:join([for(t in r.output)t.value],"")}
}
// Observable results were compared with TeX Live.
report("\\def\\foo{EXPANDED}\\tl_set:Nn\\l_tmpa_tl{A}\\tl_set:Nn\\l_tmpb_tl{B\\foo}" ++
    "\\tl_put_right:NV\\l_tmpa_tl\\l_tmpb_tl\\tl_set:Nn\\l_tmpb_tl{changed}[\\tl_to_str:V\\l_tmpa_tl]" ++
    "\\tl_set:Nn\\l_tmpb_tl{C}\\tl_put_left:NV\\l_tmpa_tl\\l_tmpb_tl[\\tl_to_str:V\\l_tmpa_tl]" ++
    "\\tl_put_right:Nv\\l_tmpa_tl{l_tmpb_tl}\\tl_put_left:Nv\\l_tmpa_tl{l_tmpb_tl}[\\tl_to_str:V\\l_tmpa_tl]" ++
    "\\begingroup\\tl_gput_right:NV\\l_tmpa_tl\\l_tmpb_tl\\tl_gput_left:Nv\\l_tmpa_tl{l_tmpb_tl}\\endgroup" ++
    "[\\tl_to_str:V\\l_tmpa_tl]")^;
report("\\int_new:N\\l_test_value_int\\int_set:Nn\\l_test_value_int{42}\\tl_set:Nn\\l_tmpa_tl{n=}" ++
    "\\tl_put_right:NV\\l_tmpa_tl\\l_test_value_int[\\tl_to_str:V\\l_tmpa_tl]")^;
report("\\tl_set:Nn\\l_tmpa_tl{A}\\tl_set:Nn\\l_tmpb_tl{B}\\let\\put\\tl_put_right:NV" ++
    "\\put\\l_tmpa_tl\\l_tmpb_tl[\\tl_to_str:V\\l_tmpa_tl]")^;
let result=profile.expand_with_kernel("\\ExplSyntaxOn\\tl_put_right:NV\\l_tmpa_tl\\l_tmpb_tl",kernel,options,100000,preload)^;
let tree=digest.digest(result)^;
tree.ast == null and tree.issue == "latex-digest-unsupported" and tree.unsupported == "expl3 value argument source mapping";
let constructor_options={*:options,constructor_argument_mode:true};
let constructor_preload=profile.preload_expl3_kernel(kernel,constructor_options,100000)^;
let guarded=profile.expand_with_kernel("\\ExplSyntaxOn\\begin{document}\\textbf{\\tl_put_right:NV\\l_tmpa_tl\\l_tmpb_tl}\\end{document}",
    kernel,constructor_options,100000,constructor_preload)^;
{issue:guarded.issue,unsupported:guarded.unsupported}
