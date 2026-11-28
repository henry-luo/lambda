import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
let preload=profile.preload_expl3_kernel(kernel,options,100000)^;
let sources = [
    "\\ExplSyntaxOn\\meaning\\use:e\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\meaning\\token_to_meaning:N\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\meaning\\tl_to_str:n\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\meaning\\token_if_macro:NTF\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\meaning\\token_if_macro:NT\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\meaning\\token_if_macro:NF\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\meaning\\token_if_macro_p:N\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\meaning\\__token_if_macro_p:w\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\meaning\\__kernel_prefix_arg_replacement:wN\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\meaning\\cs_prefix_spec:N\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\meaning\\cs_parameter_spec:N\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\meaning\\cs_replacement_spec:N\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\meaning\\__kernel_chk_defined:NT\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\meaning\\__kernel_chk_tl_type:NnnT\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\meaning\\tl_set:Ne\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\meaning\\l__tl_internal_a_tl\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\ifx\\tl_to_str:n\\detokenize Y\\else N\\fi\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\ifx\\token_to_meaning:N\\meaning Y\\else N\\fi\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\use:e{\\exp_not:N\\def\\exp_not:N\\foo{\\tl_to_str:n{ab}}}\\foo\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\let\\x\\use:e\\edef\\z{\\x{ABC}}\\z\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\def\\foo#1A#2B{Z#1#2}\\cs_prefix_spec:N\\foo/\\cs_parameter_spec:N\\foo/\\cs_replacement_spec:N\\foo\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\long\\protected\\def\\foo P#1A#2B{Z#1#2}\\cs_prefix_spec:N\\foo/\\cs_parameter_spec:N\\foo/\\cs_replacement_spec:N\\foo\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\let\\foo\\relax\\cs_prefix_spec:N\\foo/\\cs_parameter_spec:N\\foo/\\cs_replacement_spec:N\\foo\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\chardef\\foo=65 \\token_if_macro:NTF\\foo{Y}{N}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\def\\foo{abc}\\token_if_macro:NT\\foo{Y}\\token_if_macro:NF\\foo{N}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\token_if_macro:NT\\undefined{Y}\\token_if_macro:NF\\undefined{N}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\def\\foo{abc}\\edef\\x{\\token_if_macro_p:N\\foo}\\meaning\\x\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\def\\foo{abc}\\__kernel_chk_defined:NT\\foo{YES}TAIL\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\def\\foo{abc}\\__kernel_chk_tl_type:NnnT\\foo{tl}{abc}{YES}TAIL\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\def\\foo{abc}\\def\\bar{abc}\\__kernel_chk_tl_type:NnnT\\foo{tl}{\\bar}{YES}TAIL\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\def\\foo{abc}{\\__kernel_chk_tl_type:NnnT\\foo{tl}{abc}{YES}}\\meaning\\l__tl_internal_a_tl\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\def\\foo{A}\\tl_set:Ne\\l_tmpa_tl{\\foo B}\\meaning\\l_tmpa_tl\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\def\\foo{A}{\\tl_set:Ne\\l_tmpa_tl{\\foo}}\\meaning\\l_tmpa_tl\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\tl_to_str:n{a~b\\foo{é}}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\escapechar=-1 \\long\\def\\foo#1A{Z#1}\\cs_prefix_spec:N\\foo/\\cs_parameter_spec:N\\foo/\\cs_replacement_spec:N\\foo\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\escapechar=33 \\protected\\def\\foo#1{Z#1}\\cs_prefix_spec:N\\foo/\\cs_parameter_spec:N\\foo/\\cs_replacement_spec:N\\foo\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\tl_to_str:n A\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\tl_to_str:n\\foo\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\tl_to_str:n{\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\tl_to_str:n{abc\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\tl_to_str:n{a{b}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\tl_to_str:n{a% hi\\n b}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\let\\x\\tl_to_str:n\\x A\\ExplSyntaxOff"
];
fn direct(source) any^ {
    let r=profile.expand_with_kernel(source,kernel,options,100000,preload)^;
    let b=reconstruct.reconstruct(r)^;
    {text:b.text,messages:r.messages,diagnostics:r.diagnostic_records}
}
// The expansion results and diagnostics were captured with the retired native engine.
let results=[for (source in sources) direct(source)^];
results;
[for (i in 0 to len(sources) - 1) (let n=parse(sources[i],{type:"tex"})^,
    results[i].text == n.text and results[i].messages == n.messages and results[i].diagnostics == n.diagnostics)];
[for (body in ["\\__kernel_chk_defined:NT\\undefined{YES}",
    "\\def\\foo{abc}\\__kernel_chk_tl_type:NnnT\\foo{tl}{xyz}{YES}",
    "\\def\\foo#1{#1}\\__kernel_chk_tl_type:NnnT\\foo{tl}{xyz}{YES}"])
    (profile.expand_with_kernel("\\ExplSyntaxOn" ++ body ++ "\\ExplSyntaxOff",kernel,options,100000,preload)^).unsupported]
;
// A simulated primitive may hide native macro expansion while scanning a missing brace.
let lookahead_guard=(profile.expand_with_kernel("\\ExplSyntaxOn\\tl_to_str:n\\ExplSyntaxOff",kernel,options,100000,preload)^).unsupported;
// Kernel definitions have authority over the Script bootstrap.
let custom_kernel="\\catcode`\\:=11 \\catcode`\\_=11 \\def\\cs_prefix_spec:N#1{CUSTOM}\\def\\token_to_meaning:N#1{CUSTOM}\\catcode`\\:=12 \\catcode`\\_=8 " ++ kernel;
let custom=profile.preload_expl3_kernel(custom_kernel,options,100000)^;
let custom_result=profile.expand_with_kernel("\\ExplSyntaxOn\\cs_prefix_spec:N\\relax/\\token_to_meaning:N\\relax\\ExplSyntaxOff",custom_kernel,options,100000,custom)^;
{lookahead_guard:lookahead_guard,custom_text:reconstruct.reconstruct(custom_result)^.text}
