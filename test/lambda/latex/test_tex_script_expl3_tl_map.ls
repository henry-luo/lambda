// D10.2.2v2: token maps follow native grouping, nesting and break semantics.
import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true};
let preload=profile.preload_expl3_kernel(kernel,options,100000)^;
fn report(source) any^ {
    let r=profile.expand_with_kernel("\\ExplSyntaxOn\\tl_new:N\\l_result_tl" ++ source,kernel,options,100000,preload)^;
    {issue:r.issue,unsupported:r.unsupported,text:join([for(t in r.output)t.value],""),diagnostics:r.diagnostic_records}
}
// These results were compared with TeX Live, including the eight-item boundary.
report("\\tl_map_inline:nn{a~{bc}d}{\\tl_put_right:Nn\\l_result_tl{[#1]}}[\\tl_to_str:V\\l_result_tl]")^;
report("\\tl_set:Nn\\l_tmpa_tl{123456789{ab}}\\tl_map_inline:Nn\\l_tmpa_tl" ++
    "{\\tl_put_right:Nn\\l_result_tl{[#1]}}[\\tl_to_str:V\\l_result_tl]")^;
report("\\tl_map_inline:nn{ab}{\\tl_map_inline:nn{12}{\\tl_put_right:Nn\\l_result_tl{[#1/##1]}}}" ++
    "[\\tl_to_str:V\\l_result_tl]")^;
report("\\tl_map_inline:nn{abc}{\\tl_put_right:Nn\\l_result_tl{[#1]}" ++
    "\\str_if_eq:nnT{#1}{b}{\\tl_map_break:n{\\tl_put_right:Nn\\l_result_tl{END}}}}" ++
    "[\\tl_to_str:V\\l_result_tl]/\\int_use:N\\g__kernel_prg_map_int")^;
report("\\cs_new:Npn\\capture:n#1{[#1]}\\edef\\result{\\tl_map_function:nN{a{bc}d}\\capture:n}\\result")^;
let r=profile.expand_with_kernel("\\ExplSyntaxOn\\tl_map_inline:nn{a}{#1}",kernel,options,100000,preload)^;
let tree=digest.digest(r)^;
tree.ast == null and tree.issue == "latex-digest-unsupported" and tree.unsupported == "expl3 token-list mapping source mapping"
