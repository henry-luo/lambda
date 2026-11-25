// D10.2.2v2: a new input stream has TeX's global, closed terminal meaning.
import profile: lambda.doc.latex.tex_engine.profile
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true};
let preload=profile.preload_expl3_kernel(kernel,options,100000)^;
fn report(source) any^ {
    let r=profile.expand_with_kernel(source,kernel,options,100000,preload)^;
    {issue:r.issue,unsupported:r.unsupported,text:join([for(t in r.output)t.value],"")}
}
// TeX Live reports the closed stream as mathchar 16 in both forms.
report("\\ExplSyntaxOn\\begingroup\\ior_new:N\\g_test_ior\\endgroup\\meaning\\g_test_ior/" ++
    "\\int_eval:n{\\g_test_ior}/\\ior_new:c{g_other_ior}\\meaning\\g_other_ior")^;
report("\\ExplSyntaxOn\\ior_new:N\\g_test_ior\\ior_new:N\\g_test_ior")^
