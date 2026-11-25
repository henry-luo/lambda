import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct

let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let values = ["a\\%b", "a%comment\n b", "a %comment\r\n  b", "a%comment\n\n b", "%comment\n a",
    "\\textbf{a%comment\n b}", "\\foo{a\\%b}", "{a%comment\n b}", "a%]comment\n b"];
fn check(value) any^ {
    let source = "\\newcommand{\\q}[2][" ++ value ++ "]{#1/#2}\\begin{document}X\\end{document}";
    let result = profile.expand_with_kernel(source,kernel,
        {echo_definitions:true,constructor_argument_mode:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
    let native = parse(source,{type:"tex"})^;
    let tree = digest.digest(result)^;
    {text:reconstruct.reconstruct(result)^.text == native.text,
        diagnostics:result.diagnostic_records == native.diagnostics,
        tree:tree.issue == null and tree.ast == parse(source,{type:"latex"})^}
}
let checks = [for (value in values) check(value)^];
{cases:len(checks),text:all([for(c in checks)c.text]),diagnostics:all([for(c in checks)c.diagnostics]),
    tree:all([for(c in checks)c.tree])}
