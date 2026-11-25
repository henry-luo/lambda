import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct

let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let cases = ["\\def\\f#1{#1}\\f", "\\def\\f#1{#1}\\f{X", "\\def\\f#1x{#1}\\f A",
    "\\def\\f#1x{#1}\\f \\foo", "\\ExplSyntaxOn\\def\\f#1x{#1}\\f\\foo",
    "\\ExplSyntaxOn\\def\\f#1x{#1}\\f A"];
fn check(source) any^ {
    let result = profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true},100000)^;
    let native = parse(source,{type:"tex"})^;
    {text:reconstruct.reconstruct(result)^.text == native.text,
        diagnostics:result.diagnostic_records == native.diagnostics,issue:result.issue}
}
let checks = [for (source in cases) check(source)^];
{cases:len(checks),text:all([for(c in checks)c.text]),diagnostics:all([for(c in checks)c.diagnostics]),
    issues:all([for(c in checks)c.issue == null])}
