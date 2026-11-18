import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
import expansion: lambda.doc.latex.tex_engine.expansion
let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
let options = {script_expl3:true, echo_definitions:true, adapters:[], resources:{local:{}, bundled:{}}};
let baseline = parse("", {type:"tex"})^.messages;

fn check(body) any^ {
    let source = "\\ExplSyntaxOn" ++ body ++ "\\ExplSyntaxOff";
    let script = profile.expand_with_kernel(source, kernel, options, 100000)^;
    let native = parse(source, {type:"tex"})^;
    let reconstructed = reconstruct.reconstruct(script)^;
    script.issue == null and reconstructed.issue == null and reconstructed.text == native.text and
        script.messages == slice(native.messages, len(baseline), len(native.messages))^ and
        script.diagnostic_records == native.diagnostics
}

[for (body in [
    "\\def\\a{\\b}\\def\\b{abc}\\tl_head:f{\\a}/\\tl_tail:f{\\a}",
    "\\meaning\\exp_args:Nf/\\meaning\\tl_head:f/\\meaning\\tl_tail:f",
    "\\def\\a{X}\\exp_args:Nf\\use:n{a\\a}",
    "\\def\\a{X}\\def\\capture#1{\\def\\result{#1}\\meaning\\result}\\exp_args:Nf\\capture{a\\a}",
    "\\def\\a{X}\\exp_args:Nf\\use:n{~\\a}",
    "\\def\\a{X}\\exp_args:Nf\\use:n{{\\a}}",
    "\\def\\a{X}\\exp_args:Nf\\use:n{\\noexpand\\a}",
    "\\protected\\def\\a{X}\\exp_args:Nf\\use:n{\\a}",
    "\\def\\a{{ab}cd}\\tl_head:f{\\a}/\\tl_tail:f{\\a}",
    "\\def\\a{X}\\number`A\\noexpand\\a",
    "\\def\\a{abc}\\let\\h\\tl_head:f\\h{\\a}"
]) check(body)^];

let first = profile.expand_with_kernel("\\ExplSyntaxOn\\def\\a{abc}\\exp_args:Nf\\use:n{\\a}\\ExplSyntaxOff", kernel, options, 100000)^;
let guarded = digest.digest(first)^;
let resumed = expansion.resume(first, "X", "<second>", 100000)^;
let after = digest.digest(resumed)^;
[first.expl3_exp_args_f_seen, guarded.issue, guarded.unsupported,
 resumed.expl3_exp_args_f_seen, after.issue]
