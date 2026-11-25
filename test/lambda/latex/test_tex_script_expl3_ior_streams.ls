// S1.8, D10.2.2v2: expl3 streams consume only captured resource bytes.
import profile: lambda.doc.latex.tex_engine.profile
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true,resources:{local:{'ior-lines.txt':{data:"a,%{b}\n  gamma %ignored\n"}},bundled:{}}};
let preload=profile.preload_expl3_kernel(kernel,options,100000)^;
fn report(source) any^ {
    let r=profile.expand_with_kernel(source,kernel,options,100000,preload)^;
    {issue:r.issue,unsupported:r.unsupported,text:join([for(t in r.output)t.value],""),diagnostics:r.diagnostic_records}
}
// Lifecycle, slot reuse and scope were compared with TeX Live.
let lifecycle="\\ExplSyntaxOn\\ior_new:N\\g_test_ior" ++
    "\\ior_if_eof:NTF\\g_test_ior{Y}{N}|\\ior_open:Nn\\g_test_ior{ior-lines.txt}" ++
    "\\ior_if_eof:NTF\\g_test_ior{Y}{N}|\\ior_str_get:NN\\g_test_ior\\l_tmpa_tl[\\tl_to_str:V\\l_tmpa_tl]|" ++
    "\\ior_get:NN\\g_test_ior\\l_tmpa_tl[\\tl_to_str:V\\l_tmpa_tl]:\\ior_if_eof:NTF\\g_test_ior{Y}{N}|" ++
    "\\ior_str_get:NN\\g_test_ior\\l_tmpa_tl[\\tl_to_str:V\\l_tmpa_tl]:\\ior_if_eof:NTF\\g_test_ior{Y}{N}|" ++
    "\\ior_str_get:NN\\g_test_ior\\l_tmpa_tl[\\tl_to_str:V\\l_tmpa_tl]|\\ior_close:N\\g_test_ior\\ior_if_eof:NTF\\g_test_ior{Y}{N}";
let reuse="\\ExplSyntaxOn\\ior_new:N\\g_first_ior\\ior_new:N\\g_second_ior" ++
    "\\ior_open:Nn\\g_first_ior{ior-lines.txt}\\let\\saved_ior\\g_first_ior\\ior_close:N\\g_first_ior" ++
    "\\ior_open:Nn\\g_second_ior{ior-lines.txt}\\ifx\\saved_ior\\g_second_ior Y\\else N\\fi|" ++
    "\\int_eval:n{\\ior_if_eof_p:N\\g_first_ior}/\\int_eval:n{\\ior_if_eof_p:N\\g_second_ior}|" ++
    "\\begingroup\\ior_str_get:NN\\g_second_ior\\l_tmpa_tl\\endgroup" ++
    "\\ior_str_get:NN\\g_second_ior\\l_tmpa_tl[\\tl_to_str:V\\l_tmpa_tl]\\ior_close:N\\g_second_ior";
let reopen="\\ExplSyntaxOn\\ior_new:N\\g_test_ior\\ior_open:Nn\\g_test_ior{ior-lines.txt}" ++
    "\\ior_str_get:NN\\g_test_ior\\l_tmpa_tl\\ior_open:Nn\\g_test_ior{ior-lines.txt}" ++
    "\\ior_str_get:NN\\g_test_ior\\l_tmpa_tl[\\tl_to_str:V\\l_tmpa_tl]\\ior_close:N\\g_test_ior";
report(lifecycle)^;
report(reuse)^;
report(reopen)^;
report("\\ExplSyntaxOn\\ior_new:c{g_other_ior}\\ior_open:cn{g_other_ior}{ior-lines.txt}" ++
    "\\ior_str_get:NN\\g_other_ior\\l_tmpa_tl[\\tl_to_str:V\\l_tmpa_tl]\\ior_close:c{g_other_ior}")^;
report("\\ExplSyntaxOn\\ior_new:N\\g_test_ior\\ior_open:Nn\\g_test_ior{~ior-lines.txt~}" ++
    "\\ior_str_get:NN\\g_test_ior\\l_tmpa_tl[\\tl_to_str:V\\l_tmpa_tl]\\ior_close:N\\g_test_ior")^;
report("\\ExplSyntaxOn\\ior_new:N\\g_test_ior\\ior_open:Nn\\g_test_ior{ior-lines.txt}\\globaldefs=1" ++
    "\\begingroup\\ior_str_get:NN\\g_test_ior\\l_tmpa_tl\\endgroup\\globaldefs=0" ++
    "[\\tl_to_str:V\\l_tmpa_tl]\\ior_close:N\\g_test_ior")^;
report("\\ExplSyntaxOn\\ior_new:N\\g_test_ior\\globaldefs=-1\\ior_open:Nn\\g_test_ior{ior-lines.txt}")^;
report("\\ExplSyntaxOn\\ior_new:N\\g_test_ior\\ior_open:Nn\\g_test_ior{missing.txt}")^
