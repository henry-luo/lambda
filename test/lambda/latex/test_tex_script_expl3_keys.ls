// D10.2.2v2: key code and defaults use scoped TeX definitions.
import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true};
let preload=profile.preload_expl3_kernel(kernel,options,100000)^;
fn report(source) any^ {
    let r=profile.expand_with_kernel("\\ExplSyntaxOn" ++ source,kernel,options,100000,preload)^;
    {issue:r.issue,unsupported:r.unsupported,text:join([for(t in r.output)t.value],"")}
}
// These values were independently compared with TeX Live.
report("\\keys_define:nn{demo}{a.code:n=\\tl_set:Nn\\l_tmpa_tl{#1},a.initial:n=first,a.default:n=fallback}" ++
    "[\\tl_use:N\\l_tmpa_tl]\\keys_set:nn{demo}{a=second}[\\tl_use:N\\l_tmpa_tl]" ++
    "\\keys_set:nn{demo}{a}[\\tl_use:N\\l_tmpa_tl]")^;
report("\\keys_define:nn{demo}{nested~/~b.code:n={\\tl_set:Nx\\l_tmpb_tl{\\l_keys_path_str|\\l_keys_key_tl|\\l_keys_value_tl}}}" ++
    "\\keys_set:nn{demo}{nested~/~b={value}}[\\tl_use:N\\l_tmpb_tl]")^;
report("\\keys_define:nn{demo}{a.code:n=\\tl_set:Nn\\l_tmpa_tl{#1}}\\keys_set:nn{demo}{a=outer}" ++
    "\\begingroup\\keys_define:nn{demo}{a.code:n=\\tl_set:Nn\\l_tmpa_tl{local-#1}}" ++
    "\\keys_set:nn{demo}{a=value}[\\tl_use:N\\l_tmpa_tl]\\endgroup" ++
    "\\keys_set:nn{demo}{a=again}[\\tl_use:N\\l_tmpa_tl]" ++
    "\\keys_define:nn{demo}{order.code:n=\\tl_put_right:Nn\\l_tmpa_tl{(#1)},order.initial:n=one,order.initial:n=two}" ++
    "[\\tl_use:N\\l_tmpa_tl]")^;
report("\\keys_define:nn{demo}{x.tl_set:N=\\l__test_key_tl,combined.meta:n={x={A#1}}}" ++
    "\\keys_set:nn{demo}{combined={B}}[\\tl_use:N\\l__test_key_tl]")^;
report("\\keys_define:nn{inner}{i.code:n=\\tl_set:Nn\\l_tmpa_tl{#1}}" ++
    "\\keys_define:nn{demo}{outer.code:n={\\keys_set:nn{inner}{i=#1}" ++
    "\\tl_set:Nx\\l_tmpb_tl{\\l_keys_path_str|\\l_keys_key_tl|\\l_keys_value_tl}}}" ++
    "\\keys_set:nn{demo}{outer=z}[\\tl_use:N\\l_tmpa_tl][\\tl_use:N\\l_tmpb_tl]")^;
report("\\begingroup\\keys_define:nn{demo}{global.tl_gset:N=\\g__test_key_tl}" ++
    "\\keys_set:nn{demo}{global={G}}\\endgroup[\\tl_use:N\\g__test_key_tl]")^;
report("\\keys_define:nn{demo}{a.code:n=\\tl_set:Nn\\l_tmpa_tl{#1},a.default:n=x,a.default:n={}}" ++
    "\\keys_set:nn{demo}{a}[\\tl_use:N\\l_tmpa_tl]")^;
report("\\keys_define:nn{demo}{unknown.unsupported:n=x}")^;
report("\\keys_set:nn{demo}{unknown}")^;
let result=profile.expand_with_kernel("\\ExplSyntaxOn\\keys_define:nn{demo}{a.code:n={#1}}",kernel,options,100000,preload)^;
let tree=digest.digest(result)^;
tree.ast == null and tree.issue == "latex-digest-unsupported" and tree.unsupported == "expl3 key execution source mapping"
