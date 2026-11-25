import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest

let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
let cases = [
    "\\begin{document}A\\expanded{abc}Z\\end{document}",
    "\\def\\x{XY}\\begin{document}A\\expanded{\\x}Z\\end{document}",
    "\\begin{document}A\\expanded{\\textbf{X}}Z\\end{document}",
    "\\begin{document}A\\expanded{\\textit{Y} and \\emph{Z}}B\\end{document}",
    "\\begin{document}A\\expanded XZ\\end{document}",
    "\\begin{document}A\\expanded\\relax{XY}Z\\end{document}",
    "\\let\\open={\\begin{document}A\\expanded\\open X}Z\\end{document}",
    "\\let\\r=\\relax\\begin{document}A\\expanded\\r{X}Z\\end{document}",
    "\\begin{document}A\\expanded\\noexpand\\relax{X}Z\\end{document}",
    "\\begin{document}A\\expanded\\noexpand\\foo{X}Z\\end{document}",
    "\\begin{document}A\\expanded{\\noexpand\\foo}Z\\end{document}",
    "\\begin{document}A\\expanded{\\unexpanded{\\foo}}Z\\end{document}",
    "\\begin{document}A\\expanded{\\detokenize{\\foo}}Z\\end{document}",
    "\\def\\x{{Y}}\\begin{document}A\\expanded\\x Z\\end{document}",
    "\\begin{document}A\\expanded{abc",
    "\\begin{document}A\\expanded{\\textbf{X}",
    "\\begin{document}A\\expanded{",
    "\\begin{document}A\\expanded"
];
fn check(source) any^ {
    let result = profile.expand_with_kernel(source,kernel,
        {echo_definitions:true,constructor_argument_mode:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
    let native = parse(source,{type:"tex"})^;
    let actual = digest.digest(result)^;
    {text:reconstruct.reconstruct(result)^.text == native.text,
        diagnostics:result.diagnostic_records == native.diagnostics, issue:result.issue,
        tree:if (actual.issue == null) actual.ast == parse(source,{type:"latex"})^ else null}
}
let checks = [for (source in cases) check(source)^];
{cases:len(checks),text:[for (c in checks) c.text],diagnostics:[for (c in checks) c.diagnostics],
    issues:[for (c in checks) c.issue],trees:[for (c in checks) c.tree]}
let root = "test/lambda/latex/fixtures/local_packages/";
let path = root ++ "expanded-recovery-main.tex";
let child = input(root ++ "expanded-recovery-child.tex", "text")^;
let included = profile.expand_with_kernel(input(path,"text")^, kernel,
    {source_name:path,echo_definitions:true,constructor_argument_mode:true,adapters:[],
        resources:{local:{'expanded-recovery-child.tex':{data:child}},bundled:{}}},100000)^;
let native = input(path,"tex")^;
let tree = digest.digest(included)^;
{text:reconstruct.reconstruct(included)^.text == native.text,
    diagnostics:included.diagnostic_records == native.diagnostics,
    tree:tree.issue == null and tree.ast == input(path,"latex")^}
