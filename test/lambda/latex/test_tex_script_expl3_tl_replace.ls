// D10.2.2v2: replacement searches unexpanded tokens at the top level.
import profile: lambda.doc.latex.tex_engine.profile
let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^
let options = {script_expl3:true, echo_definitions:true}
let preload = profile.preload_expl3_kernel(kernel, options, 100000)^
fn report(body) any^ {
    let source = "\\ExplSyntaxOn\\tl_new:N\\l_test_tl" ++ body ++ "\\tl_to_str:V\\l_test_tl\\ExplSyntaxOff"
    let result = profile.expand_with_kernel(source, kernel, options, 100000, preload)^;
    {text:join([for (token in result.output) token.value], ""), issue:result.issue}
}
// Expected spellings were captured from TeX Live.
[for (body in [
    "\\tl_set:Nn\\l_test_tl{a,b,{c,d},e}\\tl_replace_all:Nnn\\l_test_tl{,}{+}",
    "\\tl_set:Nn\\l_test_tl{aaaaa}\\tl_replace_all:Nnn\\l_test_tl{aa}{X}",
    "\\tl_set:Nn\\l_test_tl{A\\needle: B}\\tl_replace_all:Nnn\\l_test_tl{\\needle:}{\\other:}",
    "\\tl_set:Nn\\l_test_tl{a~~b~c}\\tl_replace_all:Nnn\\l_test_tl{~}{-}",
    "\\tl_set:Nn\\l_test_tl{a,b,c}\\tl_replace_once:Nnn\\l_test_tl{,}{+}",
    "\\tl_set:Nn\\l_test_tl{aaaaa}\\tl_replace_once:Nnn\\l_test_tl{aa}{X}",
    "\\tl_set:Nn\\l_test_tl{a,b}\\begingroup\\tl_replace_all:Nnn\\l_test_tl{,}{X}\\endgroup",
    "\\tl_set:Nn\\l_test_tl{a,b}\\begingroup\\tl_greplace_all:Nnn\\l_test_tl{,}{+}\\endgroup",
    "\\tl_set:Nn\\l_test_tl{a,b}\\tl_replace_all:Nnn\\l_test_tl{,}{}",
    "\\tl_set:Nn\\l_test_tl{a,b}\\tl_replace_all:Nnn\\l_test_tl{,}{{z}}"
]) report(body)^];
// Invalid search patterns and undefined variables must not silently mutate data.
[for (source in [
    "\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_replace_all:Nnn\\l_test_tl{}{x}",
    "\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_replace_all:Nnn\\l_test_tl{{x}}{y}",
    "\\ExplSyntaxOn\\tl_replace_all:Nnn\\l_missing_tl{x}{y}"
]) (let result = profile.expand_with_kernel(source, kernel, options, 100000, preload)^,
    {issue:result.issue, unsupported:result.unsupported})]
