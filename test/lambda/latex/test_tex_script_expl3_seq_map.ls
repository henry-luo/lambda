// D10.2.2v2: sequence maps use the native callback and break-point macros.
import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true};
let preload=profile.preload_expl3_kernel(kernel,options,100000)^;
let definitions="\\ExplSyntaxOn\\seq_new:N\\l_test_seq\\seq_set_from_clist:Nn\\l_test_seq{a,{b,c},d}\\tl_new:N\\l_result_tl";
fn report(source) any^ {
    let r=profile.expand_with_kernel(definitions ++ source,kernel,options,100000,preload)^;
    {issue:r.issue,unsupported:r.unsupported,text:join([for(t in r.output)t.value],""),diagnostics:r.diagnostic_records}
}
// Map contents, nested indices, break cleanup and expandable mapping match TeX Live.
report("\\seq_map_inline:Nn\\l_test_seq{\\tl_put_right:Nn\\l_result_tl{[#1]}}[\\tl_to_str:V\\l_result_tl]")^;
report("\\seq_map_indexed_inline:Nn\\l_test_seq{\\tl_put_right:Nn\\l_result_tl{[#1:#2]}}[\\tl_to_str:V\\l_result_tl]")^;
report("\\seq_map_inline:Nn\\l_test_seq{\\seq_map_indexed_inline:Nn\\l_test_seq" ++
    "{\\tl_put_right:Nn\\l_result_tl{[#1/##1/##2]}}}[\\tl_to_str:V\\l_result_tl]")^;
report("\\seq_map_inline:Nn\\l_test_seq{\\tl_put_right:Nn\\l_result_tl{[#1]}" ++
    "\\str_if_eq:nnT{#1}{b,c}{\\seq_map_break:n{\\tl_put_right:Nn\\l_result_tl{END}}}}" ++
    "[\\tl_to_str:V\\l_result_tl]/\\int_use:N\\g__kernel_prg_map_int")^;
report("\\cs_new:Npn\\capture:nn#1#2{[#1:#2]}\\edef\\result{\\seq_map_indexed_function:NN\\l_test_seq\\capture:nn}\\result")^;
let r=profile.expand_with_kernel(definitions ++ "\\seq_map_inline:Nn\\l_test_seq{#1}",kernel,options,100000,preload)^;
let tree=digest.digest(r)^;
tree.ast == null and tree.issue == "latex-digest-unsupported" and tree.unsupported == "expl3 sequence mapping source mapping"
