// D10.2.2v2: expl3 integer assignments use TeX registers and scope.
import profile: lambda.doc.latex.tex_engine.profile
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let source="\\ExplSyntaxOn\\int_new:N\\l_test_int\\int_use:N\\l_test_int/" ++
    "\\int_set:Nn\\l_test_int{2+3*4}\\int_use:N\\l_test_int/" ++
    "\\int_incr:N\\l_test_int\\int_decr:N\\l_test_int\\int_use:N\\l_test_int/" ++
    "\\begingroup\\int_set:Nn\\l_test_int{9}\\endgroup\\int_use:N\\l_test_int/" ++
    "\\begingroup\\int_gset:Nn\\l_test_int{6}\\int_gincr:N\\l_test_int\\endgroup\\int_use:N\\l_test_int/" ++
    "\\int_zero:N\\l_test_int\\int_use:N\\l_test_int/\\int_new:c{l_other_int}\\int_set:Nn\\l_other_int{3}\\int_use:N\\l_other_int/" ++
    "\\int_gset:cn{l_other_int}{5}\\int_gincr:c{l_other_int}\\int_use:N\\l_other_int/" ++
    "\\int_gzero:c{l_other_int}\\int_use:N\\l_other_int";
// Expected values were captured independently from TeX Live.
let r=profile.expand_with_kernel(source,kernel,{script_expl3:true},100000)^;
{issue:r.issue,unsupported:r.unsupported,text:join([for(t in r.output)t.value],""),diagnostics:r.diagnostic_records}
