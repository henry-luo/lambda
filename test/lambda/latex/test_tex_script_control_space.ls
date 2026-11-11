import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn report(source) any^ {
    let exp = profile.expand_with_kernel(source, kernel, {echo_definitions: true, resources: {local: {}, bundled: {}}}, 100000)^;
    let result = digest.digest(exp)^;
    {issue: result.issue, unsupported: result.unsupported, complete: result.complete,
        matches_native: if (result.ast == null) null else result.ast == (parse(source, {type:"latex"})^)}
}
"control space:"; report("\\begin{document}A\\ B\\end{document}")
"control space with following spaces:"; report("\\begin{document}A\\  B\\end{document}")
"macro-expanded control space:"; report("\\def\\sp{\\ }\\begin{document}A\\sp B\\end{document}")
"control space in formatting:"; report("\\begin{document}\\textbf{A\\ B}\\end{document}")
"control space before comment:"; report("\\begin{document}A\\  %comment\n B\\end{document}")
"control space before CRLF comment:"; report("\\begin{document}A\\  %comment\r\n B\\end{document}")
