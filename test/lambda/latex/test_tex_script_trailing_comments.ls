import profile: lambda.doc.latex.tex_engine.profile
import native_snapshot: ._tex_native_snapshot

let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
// Retain native comparisons after the public parser switched to Script.
let native = input("test/lambda/latex/fixtures/tex_trailing_comments_native.json","json")^;
let tails = ["% tail\nX", "a%comment\nB", "%a\n\nB", "a%a\r\nB", "% tail", "a%tail\n\nB",
    "\n\n", "\n\nA", "a\n\n", " \n\n", "%x\n\n\n", "a\n\n%b\n", "%x\r\n\r\nB", "a%one\n%two\nB",
    "% \\foo { $ }\nX", "a% \\command {x}\nB"];
fn check(tail, reference) any^ {
    let source = "\\begin{document}A\\end{document}" ++ tail;
    let result = profile.expand_with_kernel(source,kernel,
        {echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
    let comparison = native_snapshot.compare(result,reference,source)^;
    {text:comparison.text_match,
        diagnostics:source == reference.source and result.diagnostic_records == reference.tex.diagnostics,
        tree:comparison.issue == null and comparison.ast_match == true}
}
let checks = [for (index,tail in tails) check(tail,native.cases[index])^];
{cases:len(checks),text:all([for(c in checks)c.text]),diagnostics:all([for(c in checks)c.diagnostics]),
    tree:all([for(c in checks)c.tree])}
