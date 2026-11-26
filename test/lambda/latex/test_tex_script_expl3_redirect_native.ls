import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
let preload=profile.preload_expl3_kernel(kernel,options,100000)^;
let sources = [
    "\\ExplSyntaxOn\\msg_redirect_name:nnn{kernel}{variable-not-defined}{none}\\__kernel_chk_defined:NT\\undefined{YES}TAIL\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_redirect_name:nnn{kernel}{variable-not-defined}{none}\\prop_remove:Nn\\l_missing_prop{a}\\prop_if_exist:NTF\\l_missing_prop{Y}{N}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_redirect_name:nnn{kernel}{variable-not-defined}{none}{\\prop_remove:Nn\\l_missing_prop{a}}\\prop_if_exist:NTF\\l_missing_prop{Y}{N}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_redirect_name:nnn{kernel}{variable-not-defined}{none}\\prop_get:NnN\\l_missing_prop{a}\\l_tmpa_tl\\meaning\\l_tmpa_tl\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_redirect_name:nnn{kernel}{bad-type}{none}\\def\\l_wrong_prop{abc}\\prop_remove:Nn\\l_wrong_prop{a}\\prop_if_empty:NTF\\l_wrong_prop{Y}{N}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_redirect_name:nnn{kernel}{bad-type}{none}\\def\\foo#1{#1}\\__kernel_chk_tl_type:NnnT\\foo{tl}{abc}{YES}TAIL\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_redirect_name:nnn{kernel}{bad-type}{none}\\def\\foo{abc}\\__kernel_chk_tl_type:NnnT\\foo{tl}{xyz}{YES}TAIL\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_new:nnn{my}{oops}{Oh~#1}\\msg_redirect_name:nnn{my}{oops}{none}\\msg_error:nnn{my}{oops}{X}TAIL\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_new:nnn{my}{oops}{Oh~#1}\\msg_redirect_name:nnn{my}{oops}{none}\\let\\e\\msg_error:nnn\\e{my}{oops}{X}TAIL\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_redirect_name:nnn{kernel}{variable-not-defined}{none}\\meaning\\l__msg_redirect_prop\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\meaning\\msg_redirect_name:nnn/\\meaning\\__msg_class_chk_exist:nT/\\meaning\\__msg_none_code:nnnnnn\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\meaning\\msg_error:nn/\\meaning\\msg_error:nnn/\\meaning\\msg_error:nnnnnn\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_redirect_name:nnn{missing}{nothing}{none}\\prop_get:NnN\\l__msg_redirect_prop{/missing/nothing}\\l_tmpa_tl\\meaning\\l_tmpa_tl\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_redirect_name:nnn{kernel}{variable-not-defined}{none}\\msg_redirect_name:nnn{kernel}{variable-not-defined}{}\\prop_if_empty:NTF\\l__msg_redirect_prop{Y}{N}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_redirect_class:nn{error}{none}\\__kernel_chk_defined:NT\\undefined{YES}TAIL\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_redirect_module:nnn{kernel}{error}{none}\\__kernel_chk_defined:NT\\undefined{YES}TAIL\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_redirect_module:nnn{kernel}{error}{none}\\prop_remove:Nn\\l_missing_prop{a}\\prop_if_exist:NTF\\l_missing_prop{Y}{N}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_new:nnn{my/sub}{oops}{Oops}\\msg_redirect_module:nnn{my}{error}{none}\\msg_error:nn{my/sub}{oops}TAIL\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_redirect_class:nn{error}{none}\\prop_get:NnN\\l__msg_redirect_error_prop{}\\l_tmpa_tl\\meaning\\l_tmpa_tl\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_redirect_module:nnn{kernel}{error}{none}\\prop_get:NnN\\l__msg_redirect_error_prop{/kernel}\\l_tmpa_tl\\meaning\\l_tmpa_tl\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\meaning\\msg_redirect_class:nn/\\meaning\\msg_redirect_module:nnn/\\meaning\\__msg_redirect:nnn\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_redirect_class:nn{error}{none}\\globaldefs=-1 {\\prop_remove:Nn\\l_missing_prop{k}}\\globaldefs=0 \\prop_if_exist:NTF\\l_missing_prop{Y}{N}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_redirect_class:nn{error}{none}\\globaldefs=1 {\\prop_remove:Nn\\l_missing_prop{k}}\\globaldefs=0 \\prop_if_exist:NTF\\l_missing_prop{Y}{N}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_redirect_class:nn{error}{none}{\\prop_get:NnN\\l_missing_prop{k}\\l_tmpa_tl}\\prop_if_exist:NTF\\l_missing_prop{Y}{N}/\\meaning\\l_tmpa_tl\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_redirect_class:nn{error}{none}\\prop_put:Nnn\\l_missing_prop{k}{v}\\prop_if_in:NnTF\\l_missing_prop{k}{Y}{N}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_redirect_name:nnn{kernel}{bad-type}{none}\\def\\l_p_prop{abc}\\prop_get:NnN\\l_p_prop{k}\\l_tmpa_tl\\prop_if_empty:NTF\\l_p_prop{Y}{N}/\\meaning\\l_tmpa_tl\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_redirect_class:nn{error}{none}\\def\\l_p_prop#1{#1}\\prop_pop:NnN\\l_p_prop{k}\\l_tmpa_tl\\prop_if_empty:NTF\\l_p_prop{Y}{N}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_new:nnn{my/sub}{oops}{Oh~#1~#2}\\msg_redirect_class:nn{error}{warning}\\msg_redirect_class:nn{warning}{none}\\def\\x{A}\\msg_error:nnee{my/sub}{oops}{\\x}{B}TAIL\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_new:nnn{my/sub}{oops}{Oh}\\msg_redirect_class:nn{error}{log}\\msg_redirect_class:nn{log}{none}\\msg_error:nn{my/sub}{oops}TAIL\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_redirect_class:nn{error}{none}\\let\\p\\prop_remove:Nn\\p\\l_missing_prop{k}\\prop_if_exist:NTF\\l_missing_prop{Y}{N}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\prop_new:N\\l_p_prop\\prop_if_empty:NTF\\l_p_prop{Y}{N}\\prop_put:Nnn\\l_p_prop{k}{v}\\prop_if_empty:cTF{l_p_prop}{Y}{N}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{k}{v}\\prop_remove:Nn\\l_p_prop{k}\\edef\\x{\\prop_if_empty_p:N\\l_p_prop}\\meaning\\x\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{k}{v}\\let\\t\\prop_if_empty:NTF\\t\\l_p_prop{Y}{N}\\ExplSyntaxOff"
];
fn direct(source) any^ {
    let r=profile.expand_with_kernel(source,kernel,options,100000,preload)^;
    let b=reconstruct.reconstruct(r)^;
    {text:b.text,messages:r.messages,diagnostics:r.diagnostic_records,issue:r.issue}
}
// results and diagnostics come from the retired native parser.
let results=[for (source in sources) direct(source)^];
[for (result in results) {text:result.text,messages:result.messages,diagnostics:result.diagnostics}];
[for (i in 0 to len(sources) - 1) (let n=parse(sources[i],{type:"tex"})^,
    results[i].issue == null and results[i].text == n.text and
    results[i].messages == n.messages and results[i].diagnostics == n.diagnostics)];
let guarded=["\\ExplSyntaxOn\\prop_remove:Nn\\l_missing_prop{k}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_redirect_class:nn{error}{none}\\msg_redirect_class:nn{error}{}\\__kernel_chk_defined:NT\\undefined{YES}TAIL\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\msg_redirect_class:nn{error}{warning}\\msg_redirect_class:nn{warning}{error}\\ExplSyntaxOff"];
[for (source in guarded) (profile.expand_with_kernel(source,kernel,options,100000,preload)^).unsupported];
// public parsing must reject cases whose native error rendering is still unported.
[for (source in guarded) (parse(source,{type:"tex"}) ^ {null}) == null]
