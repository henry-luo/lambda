import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct

let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let tails = ["% tail\nX", "a%comment\nB", "%a\n\nB", "a%a\r\nB", "% tail", "a%tail\n\nB",
    "\n\n", "\n\nA", "a\n\n", " \n\n", "%x\n\n\n", "a\n\n%b\n", "%x\r\n\r\nB", "a%one\n%two\nB",
    "% \\foo { $ }\nX", "a% \\command {x}\nB"];
fn check(tail) any^ {
    let source = "\\begin{document}A\\end{document}" ++ tail;
    let result = profile.expand_with_kernel(source,kernel,
        {echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
    let native = parse(source,{type:"tex"})^;
    let tree = digest.digest(result)^;
    {text:reconstruct.reconstruct(result)^.text == native.text,
        diagnostics:result.diagnostic_records == native.diagnostics,
        tree:tree.issue == null and tree.ast == parse(source,{type:"latex"})^}
}
let checks = [for (tail in tails) check(tail)^];
{cases:len(checks),text:all([for(c in checks)c.text]),diagnostics:all([for(c in checks)c.diagnostics]),
    tree:all([for(c in checks)c.tree])}
