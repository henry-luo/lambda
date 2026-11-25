import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct

let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let cases = ["\\end", "A\\end", "\\begin", "\\iftrue A\\end", "\\end ", "\\end{", "\\end{foo",
    "\\end}", "\\begin{", "\\begin{foo", "\\begin}", "\\begin{abc}X\\end",
    "\\begin{abc}X\\end{abc", "\\begin{abc}X\\end}", "\\iftrue\\begin{abc}X\\end{abc"];
fn check(source) any^ {
    let result = profile.expand_with_kernel(source,kernel,
        {echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
    let native = parse(source,{type:"tex"})^;
    {text:reconstruct.reconstruct(result)^.text == native.text,
        diagnostics:result.diagnostic_records == native.diagnostics,issue:result.issue}
}
let checks = [for (source in cases) check(source)^];
{cases:len(checks),text:all([for(c in checks)c.text]),diagnostics:all([for(c in checks)c.diagnostics]),
    issues:all([for(c in checks)c.issue == null])}
