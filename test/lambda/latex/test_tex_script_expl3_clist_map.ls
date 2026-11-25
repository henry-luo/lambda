// D10.2.2v2: inline comma-list maps retain TeX token and nesting semantics.
import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true};
let preload=profile.preload_expl3_kernel(kernel,options,100000)^;
fn report(source) any^ {
    let r=profile.expand_with_kernel("\\ExplSyntaxOn" ++ source,kernel,options,100000,preload)^;
    {issue:r.issue,unsupported:r.unsupported,text:join([for(t in r.output)t.value],"")}
}
// Values, break behavior and stored list form were compared with TeX Live.
report("\\tl_new:N\\l_result_tl\\clist_map_inline:nn{a,~{b,c},~{d},~,~{~e~},,}" ++
    "{\\tl_put_right:Nn\\l_result_tl{[#1]}}[\\tl_to_str:V\\l_result_tl]/[\\tl_to_str:V\\l__clist_internal_clist]")^;
report("\\tl_new:N\\l_result_tl\\clist_map_inline:nn{a,b,c}{\\tl_put_right:Nn\\l_result_tl{[#1]}" ++
    "\\str_if_eq:nnT{#1}{b}{\\clist_map_break:n{END}}}[\\tl_to_str:V\\l_result_tl]")^;
report("\\tl_new:N\\l_result_tl\\clist_map_inline:nn{a,b}{\\clist_map_inline:nn{1,2}" ++
    "{\\tl_put_right:Nn\\l_result_tl{[#1/##1]}}}[\\tl_to_str:V\\l_result_tl]")^;
report("\\tl_set:Nn\\l_tmpa_tl{X,{Y,Z},{} ,{{Q}}}\\tl_new:N\\l_result_tl" ++
    "\\clist_map_inline:Nn\\l_tmpa_tl{\\tl_put_right:Nn\\l_result_tl{[#1]}}[\\tl_to_str:V\\l_result_tl]")^;
report("\\tl_new:N\\l_result_tl\\clist_map_inline:nn{a}{\\tl_put_right:Nx\\l_result_tl{\\int_use:N\\g__kernel_prg_map_int}" ++
    "\\clist_map_inline:nn{b}{\\tl_put_right:Nx\\l_result_tl{\\int_use:N\\g__kernel_prg_map_int}}" ++
    "\\tl_put_right:Nx\\l_result_tl{\\int_use:N\\g__kernel_prg_map_int}}" ++
    "[\\tl_to_str:V\\l_result_tl]/\\int_use:N\\g__kernel_prg_map_int")^;
report("\\clist_map_inline:nn{}{SHOULD-NOT-RUN}\\int_use:N\\g__kernel_prg_map_int")^;
let result=profile.expand_with_kernel("\\ExplSyntaxOn\\clist_map_inline:nn{a}{#1}",kernel,options,100000,preload)^;
let tree=digest.digest(result)^;
tree.ast == null and tree.issue == "latex-digest-unsupported" and tree.unsupported == "expl3 comma-list mapping source mapping"
