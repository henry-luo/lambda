import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest

let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let cases = ["\\begin{document}A\\expanded{{abc", "\\begin{document}A\\expanded{{{abc",
    "\\begin{document}A\\expanded{\\textbf{abc", "\\expanded{{abc", "\\begin{document}A{abc"];
fn check(source) any^ {
    let result = profile.expand_with_kernel(source,kernel,
        {echo_definitions:true,constructor_argument_mode:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
    let native = parse(source,{type:"tex"})^;
    let tree = digest.digest(result)^;
    {text:reconstruct.reconstruct(result)^.text == native.text,
        diagnostics:result.diagnostic_records == native.diagnostics, issue:tree.issue}
}
let checks = [for (source in cases) check(source)^];
{cases:len(checks),text:all([for(c in checks)c.text]),diagnostics:all([for(c in checks)c.diagnostics]),
    issues:[for(c in checks)c.issue]}
