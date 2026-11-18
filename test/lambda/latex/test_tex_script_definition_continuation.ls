import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct

let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
let defaults = ["a[b]c", "[a]", "a[[b]]c", "[a][b]", "a{[b]}c", "a[b] c",
    "a[b]\\textbf{c}", "a[b]é", "a[b]c[d]e", "a[b]\\number12c", "a[b]\\count0 c"];
fn compare(value, mode) any^ {
    let source = "\\newcommand{\\q}[2][" ++ value ++ "]{#1/#2}\\begin{document}\\q{B}\\end{document}";
    let expanded = profile.expand_with_kernel(source,kernel,
        {echo_definitions:true, constructor_argument_mode:mode, adapters:[], resources:{local:{},bundled:{}}},100000)^;
    let result = digest.digest(expanded)^;
    let native = parse(source,{type:"tex"})^;
    {issue:expanded.issue, digest_issue:result.issue, tree:result.ast == parse(source,{type:"latex"})^,
        text:reconstruct.reconstruct(expanded)^.text == native.text,
        diagnostics:expanded.diagnostic_records == native.diagnostics}
}
let results = [for (value in defaults, mode in [false,true]) compare(value,mode)^];
{cases:len(results), expanded:all([for (r in results) r.issue==null]),
 digested:all([for (r in results) r.digest_issue==null]),
 tree:all([for (r in results) r.tree]), text:all([for (r in results) r.text]),
 diagnostics:all([for (r in results) r.diagnostics])}
