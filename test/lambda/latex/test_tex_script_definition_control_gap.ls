import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct

let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
let suffixes = ["]bar", "]", "]x"];
let gaps = ["", " ", "  ", "\n", "\n\n", "%comment\n ", "%comment\n\n ", " %comment\r\n  "];
fn check(suffix, gap) any^ {
    let source = "\\catcode`\\]=11 \\def\\x{\\foo" ++ suffix ++ gap ++ "Q}\\catcode`\\]=12 \\begin{document}X\\end{document}";
    let expanded = profile.expand_with_kernel(source, kernel,
        {echo_definitions:true, constructor_argument_mode:true, adapters:[], resources:{local:{},bundled:{}}},100000)^;
    let actual = digest.digest(expanded)^;
    let native = parse(source,{type:"tex"})^;
    {tree:actual.issue == null and actual.ast == parse(source,{type:"latex"})^,
        text:reconstruct.reconstruct(expanded)^.text == native.text,
        diagnostics:expanded.diagnostic_records == native.diagnostics}
}
let checks = [for (suffix in suffixes, gap in gaps) check(suffix,gap)^];
{cases:len(checks),tree:all([for (c in checks) c.tree]),text:all([for (c in checks) c.text]),
    diagnostics:all([for (c in checks) c.diagnostics])}
