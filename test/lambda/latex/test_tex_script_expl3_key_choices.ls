// D10.2.2v2: choices dispatch through scoped key macros and TeX registers.
import profile: lambda.doc.latex.tex_engine.profile
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true};
let preload=profile.preload_expl3_kernel(kernel,options,100000)^;
fn report(source) any^ {
    let r=profile.expand_with_kernel("\\ExplSyntaxOn" ++ source,kernel,options,100000,preload)^;
    {issue:r.issue,unsupported:r.unsupported,text:join([for(t in r.output)t.value],"")}
}
// The observable values were compared with TeX Live.
report("\\keys_define:nn{demo}{color.choices:nn={red,blue,green}" ++
    "{\\tl_set:Nx\\l_tmpa_tl{#1|\\l_keys_choice_tl|\\int_use:N\\l_keys_choice_int}},color.initial:n=blue}" ++
    "[\\tl_use:N\\l_tmpa_tl]\\keys_set:nn{demo}{color=green}[\\tl_use:N\\l_tmpa_tl]")^;
report("\\keys_define:nn{demo}{enabled.bool_set:N=\\l__test_bool,inverse.bool_set_inverse:N=\\l__inverse_bool}" ++
    "\\keys_set:nn{demo}{enabled,inverse=true}\\bool_if:NTF\\l__test_bool{T}{F}/\\bool_if:NTF\\l__inverse_bool{T}{F}|" ++
    "\\keys_set:nn{demo}{enabled=false,inverse=false}\\bool_if:NTF\\l__test_bool{T}{F}/\\bool_if:NTF\\l__inverse_bool{T}{F}")^;
report("\\keys_define:nn{demo}{g.bool_gset:N=\\g__test_bool}\\begingroup\\keys_set:nn{demo}{g=true}\\endgroup" ++
    "\\bool_if:NTF\\g__test_bool{T}{F}")^;
report("\\keys_define:nn{demo}{color.choices:nn={red,blue}{\\tl_set:Nn\\l_tmpa_tl{#1}}}" ++
    "\\tl_set:Nn\\l_keys_choice_tl{saved}\\int_set:Nn\\l_keys_choice_int{42}" ++
    "\\keys_set:nn{demo}{color=red}[\\tl_use:N\\l_keys_choice_tl]/\\int_use:N\\l_keys_choice_int" ++
    "\\keys_define:nn{demo}{color~/~unknown.code:n=\\tl_set:Nn\\l_tmpa_tl{fallback-#1}}" ++
    "\\keys_set:nn{demo}{color=purple}[\\tl_use:N\\l_tmpa_tl]")^;
report("\\keys_define:nn{demo}{color.choices:nn={red,blue}{#1}}\\keys_set:nn{demo}{color=unknown}")^
