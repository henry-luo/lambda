import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn report(source) any^ {
    let exp = profile.expand_with_kernel(source, kernel, {echo_definitions: true, resources: {local: {}, bundled: {}}}, 100000)^;
    let result = digest.digest(exp)^;
    {issue: result.issue, unsupported: result.unsupported, complete: result.complete,
        matches_native: if (result.ast == null) null else result.ast == (parse(source, {type:"latex"})^)}
}
"underline:"; report("\\begin{document}A\\underline{Hello}B\\end{document}")
"nested underline:"; report("\\begin{document}\\underline{\\textbf{Bold}}\\end{document}")
"empty underline:"; report("\\begin{document}\\underline{}\\end{document}")
"underline with whitespace:"; report("\\begin{document}\\underline  {Hello}\\end{document}")
"math underline:"; report("\\begin{document}$\\underline{x}$\\end{document}")
"expanded underline source mapping:"; report("\\def\\u{\\underline{Hi}}\\begin{document}\\u\\end{document}")
"math overline:"; report("\\begin{document}$\\overline{x}$\\end{document}")
"math underline expression:"; report("\\begin{document}$\\underline{a+b}$\\end{document}")
"math underline ungrouped:"; report("\\begin{document}$\\underline x$\\end{document}")
"expanded bold source mapping:"; report("\\def\\u{\\textbf{Hi}}\\begin{document}\\u\\end{document}")
"text overline:"; report("\\begin{document}\\overline{a}\\end{document}")
import reader: lambda.doc.latex.tex_engine.reader
import stream: lambda.doc.latex.tex_engine.stream
import expansion: lambda.doc.latex.tex_engine.expansion
fn tex_tokens(source) any^ {
    let r = {*:reader.reader(source), endline: -1, catcodes: {'{': 1, '}': 2, '#': 6}};
    let result = expansion.expand(stream.from_reader(r), 100)^;
    {issue: result.issue, tokens: [for (t in result.output) [t.kind, t.value]]}
}
"tex accents:"; tex_tokens("\\underline{x}\\overline{y}")
"tex accent alias:"; tex_tokens("\\let\\a=\\underline\\a{x}")
