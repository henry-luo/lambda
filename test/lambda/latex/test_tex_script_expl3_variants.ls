// D10.2.2v2: generated variants retain TeX expansion and macro identity.
import profile: lambda.doc.latex.tex_engine.profile
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true};
let preload=profile.preload_expl3_kernel(kernel,options,100000)^;
fn report(source) any^ {
    let r=profile.expand_with_kernel(source,kernel,options,100000,preload)^;
    {issue:r.issue,unsupported:r.unsupported,text:join([for(t in r.output)t.value],"")}
}
// The expected outputs were captured independently from TeX Live.
let general="\\ExplSyntaxOn\\tl_new:N\\l_a_tl\\tl_set:Nn\\l_a_tl{A}\\tl_new:N\\l_b_tl\\tl_set:Nn\\l_b_tl{B}" ++
"\\cs_new:Npn\\demo:Nnn#1#2#3{[#2/#3]}\\begingroup\\cs_generate_variant:Nn\\demo:Nnn{NVV,NV,NnV,cVn,Nne,Nno,Nnv}\\endgroup" ++
"\\demo:NVV\\relax\\l_a_tl\\l_b_tl|\\demo:NVn\\relax\\l_a_tl{C}|\\demo:NnV\\relax{C}\\l_b_tl|" ++
"\\demo:cVn{relax}\\l_a_tl{C}|\\demo:Nnv\\relax{C}{l_b_tl}|\\cs_new:Npn\\word:{EXP}" ++
"\\demo:Nne\\relax{C}{\\word:}|\\demo:Nno\\relax{C}{\\word:}|\\meaning\\demo:NVV|" ++
"\\let\\alias\\demo:NVV\\cs_set:Npn\\demo:Nnn#1#2#3{(#2+#3)}\\alias\\relax\\l_a_tl\\l_b_tl\\ExplSyntaxOff";
let edges="\\ExplSyntaxOn\\cs_new:Npn\\first:{\\second:}\\cs_new:Npn\\second:{Z}" ++
"\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{\\first:}\\cs_new:Npn\\probe:n#1{\\tl_to_str:n{#1}}" ++
"\\cs_generate_variant:Nn\\probe:n{o,e,V,v}\\probe:o{\\first:}|\\probe:e{\\first:}|\\probe:V\\l_test_tl|\\probe:v{l_test_tl}|" ++
"\\cs_new_protected:Npn\\guard:Nn#1#2{[#2]}\\cs_generate_variant:Nn\\guard:Nn{NV}\\meaning\\guard:NV|" ++
"\\cs_generate_variant:Nn\\tl_if_blank:nF{V}\\tl_if_blank:VF\\l_test_tl{YES}\\ExplSyntaxOff";
report(general)^;
report(edges)^;
report("\\ExplSyntaxOn\\tl_new:N\\l_separator_tl\\tl_set:Nn\\l_separator_tl{,}" ++
    "\\tl_new:N\\l_source_tl\\tl_set:Nn\\l_source_tl{a,b,c}\\seq_new:N\\l_items_seq" ++
    "\\cs_generate_variant:Nn\\seq_set_split:Nnn{NVV}\\seq_set_split:NVV\\l_items_seq\\l_separator_tl\\l_source_tl" ++
    "\\seq_use:Nn\\l_items_seq{|}/\\cs_generate_variant:Nn\\tl_replace_all:Nnn{NV}" ++
    "\\tl_replace_all:NVn\\l_source_tl\\l_separator_tl{+}\\tl_use:N\\l_source_tl\\ExplSyntaxOff")^;
// Unsupported signatures and missing bases report failures instead of inventing variants.
[for (source in ["\\ExplSyntaxOn\\cs_generate_variant:Nn\\missing:n{V}",
    "\\ExplSyntaxOn\\cs_new:Npn\\demo:n#1{#1}\\cs_generate_variant:Nn\\demo:n{x}",
    "\\ExplSyntaxOn\\cs_new:Npn\\demo:N#1{#1}\\cs_generate_variant:Nn\\demo:N{V}"])
    (let r=profile.expand_with_kernel(source,kernel,options,100000,preload)^,
     {issue:r.issue,unsupported:r.unsupported})]
