// D10.2.2v2: expl3 boolean expressions use TeX's expandable operator grammar.
import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true,resources:{local:{'bool-stream.txt':{data:"alpha\nbeta\n"}},bundled:{}}};
let preload=profile.preload_expl3_kernel(kernel,options,100000)^;
fn report(source) any^ {
    let r=profile.expand_with_kernel("\\ExplSyntaxOn" ++ source,kernel,options,100000,preload)^;
    {issue:r.issue,unsupported:r.unsupported,text:join([for(t in r.output)t.value],""),diagnostics:r.diagnostic_records}
}
// Expression precedence, all four loops, and EOF behavior were compared with TeX Live.
report("\\bool_new:N\\l_a_bool\\bool_new:N\\l_b_bool\\bool_set_true:N\\l_a_bool\\bool_set_false:N\\l_b_bool" ++
    "\\bool_if:nTF{\\l_a_bool}{T}{F}/\\bool_if:nTF{!\\l_a_bool}{T}{F}/" ++
    "\\bool_if:nTF{\\l_a_bool&& !\\l_b_bool}{T}{F}/" ++
    "\\bool_if:nTF{\\c_true_bool || \\c_false_bool && \\c_false_bool}{T}{F}/" ++
    "\\bool_if:nTF{(\\c_true_bool || \\c_false_bool) && \\c_false_bool}{T}{F}")^;
let definitions="\\int_new:N\\l_test_int\\cs_new:Npn\\less_three:{\\int_compare:nNnTF{\\l_test_int}<{3}{\\c_true_bool}{\\c_false_bool}}" ++
    "\\cs_new:Npn\\at_three:{\\int_compare:nNnTF{\\l_test_int}>{2}{\\c_true_bool}{\\c_false_bool}}";
report(definitions ++ "\\int_zero:N\\l_test_int\\bool_while_do:nn{\\less_three:}{\\int_incr:N\\l_test_int}\\int_use:N\\l_test_int/" ++
    "\\int_zero:N\\l_test_int\\bool_until_do:nn{\\at_three:}{\\int_incr:N\\l_test_int}\\int_use:N\\l_test_int/" ++
    "\\int_zero:N\\l_test_int\\bool_do_while:nn{\\c_false_bool}{\\int_incr:N\\l_test_int}\\int_use:N\\l_test_int/" ++
    "\\int_zero:N\\l_test_int\\bool_do_until:nn{\\c_true_bool}{\\int_incr:N\\l_test_int}\\int_use:N\\l_test_int")^;
report("\\ior_new:N\\g_test_ior\\ior_open:Nn\\g_test_ior{bool-stream.txt}\\tl_clear:N\\l_tmpb_tl" ++
    "\\bool_until_do:nn{\\ior_if_eof_p:N\\g_test_ior}{\\ior_str_get:NN\\g_test_ior\\l_tmpa_tl" ++
    "\\tl_put_right:Nn\\l_tmpb_tl{[}\\tl_put_right:NV\\l_tmpb_tl\\l_tmpa_tl\\tl_put_right:Nn\\l_tmpb_tl{]}}" ++
    "\\ior_close:N\\g_test_ior\\tl_to_str:V\\l_tmpb_tl")^;
let result=profile.expand_with_kernel("\\ExplSyntaxOn\\bool_if:nTF{\\c_true_bool}{Y}{N}",kernel,options,100000,preload)^;
let tree=digest.digest(result)^;
tree.ast == null and tree.issue == "latex-digest-unsupported" and tree.unsupported == "expl3 boolean expression source mapping"
