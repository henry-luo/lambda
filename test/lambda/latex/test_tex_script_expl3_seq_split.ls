// D10.2.2v2: sequence splitting follows expl3's token and scope semantics.
import profile: lambda.doc.latex.tex_engine.profile
let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^
let options = {script_expl3:true, echo_definitions:true}
let preload = profile.preload_expl3_kernel(kernel, options, 100000)^
fn report(body) any^ {
    let source = "\\ExplSyntaxOn\\seq_new:N\\l_test_seq" ++ body ++
        "\\seq_count:N\\l_test_seq/\\seq_use:Nn\\l_test_seq{|}\\ExplSyntaxOff"
    let result = profile.expand_with_kernel(source, kernel, options, 100000, preload)^;
    {text:join([for (token in result.output) token.value], ""), issue:result.issue}
}
// Expected strings were captured from TeX Live's expl3 implementation.
[for (body in [
    "\\seq_set_split:Nnn\\l_test_seq{,}{~a,,{b,c},~{d}~}",
    "\\seq_set_split:Nnn\\l_test_seq{,}{}",
    "\\seq_set_split:Nnn\\l_test_seq{}{ab{c~d}~e}",
    "\\seq_set_split:Nnn\\l_test_seq{::}{a::b:::c::}",
    "\\seq_set_split_keep_spaces:Nnn\\l_test_seq{,}{~{d}~,~~,x~}",
    "\\seq_set_split:Nnn\\l_test_seq{ab}{a{b}ab{ab}ab}",
    "\\seq_set_split:Nnn\\l_test_seq{\\cut:}{A\\cut: B}",
    "\\seq_set_split:Nnn\\l_test_seq{~}{a~{~b~}~c}",
    "\\seq_set_split:Nnn\\l_test_seq{,}{A}\\begingroup\\seq_set_split:Nnn\\l_test_seq{,}{B,C}\\endgroup",
    "\\begingroup\\seq_gset_split:Nnn\\l_test_seq{,}{B,C}\\endgroup"
]) report(body)^]
