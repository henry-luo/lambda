import utf8: lambda.io.utf8
import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn render(token) => if (token.kind == "control") "\\" ++ token.value else token.value
fn render_tokens(tokens, source, index = 0) any^ {
    if (index >= len(tokens)) ""
    else {
        let token = tokens[index];
        if (token.echo_span != null)
            utf8.decode(slice(binary(source)^, token.echo_span.start, token.echo_span.end)^)^ ++
                render_tokens(tokens, source, index + token.echo_count)^
        else render(token) ++ render_tokens(tokens, source, index + 1)^
    }
}
fn matches(source) any^ {
    let result = profile.expand_with_kernel(source, kernel,
        {echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let text = render_tokens(result.output, source)^;
    let native = parse(source, {type: "tex"})^;
    {issue: result.issue, matches: text == native.text, diagnostics_match: result.diagnostic_records == native.diagnostics, messages_empty: len(result.messages) == 0}
}
let sources = [
 "\\begin{document}A\\the\\parshape B\\end{document}",
 "\\begin{document}A\\the\\interlinepenalties0 B\\end{document}",
 "\\begin{document}A\\the\\clubpenalties1 B\\end{document}",
 "\\begin{document}A\\the\\widowpenalties-2 B\\end{document}",
 "\\begin{document}A\\the\\displaywidowpenalties42 B\\end{document}",
 "\\parshape1 1pt 2pt\\begin{document}A\\the\\parshape B\\end{document}",
 "\\clubpenalties2 10 20\\begin{document}A\\the\\clubpenalties2 B\\end{document}",
 "\\begin{document}A\\the\\numexpr2+\\parshape\\relax B\\end{document}",
 "\\begin{document}A\\the\\numexpr2+\\clubpenalties1\\relax B\\end{document}",
 "\\count0=\\parshape\\begin{document}A\\the\\count0 B\\end{document}",
 "\\count0=\\widowpenalties-3\\begin{document}A\\the\\count0 B\\end{document}",
 "\\begin{document}A\\ifnum\\parshape=0 T\\else F\\fi B\\end{document}",
 "\\begin{document}A\\ifnum\\clubpenalties1=0 T\\else F\\fi B\\end{document}",
 "\\let\\x=\\parshape\\begin{document}A\\the\\x B\\end{document}",
 "\\let\\x=\\clubpenalties\\begin{document}A\\the\\x2 B\\end{document}",
 "\\count0=1\\begin{document}A\\the\\clubpenalties\\count0 B\\end{document}",
 "\\def\\i{2 }\\begin{document}A\\the\\widowpenalties\\i B\\end{document}",
 "\\dimen0=\\parshape pt\\begin{document}A\\the\\dimen0 B\\end{document}",
 "\\dimen0=\\clubpenalties1 pt\\begin{document}A\\the\\dimen0 B\\end{document}",
 "\\begin{document}A\\the\\numexpr\\clubpenalties\\numexpr1+1\\relax+4\\relax B\\end{document}"
];
[for (source in sources) matches(source)^]

fn digestion(source) any^ {
    let expanded = profile.expand_with_kernel(source, kernel,
        {echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let actual = digest.digest(expanded)^;
    {issue: actual.issue, unsupported: actual.unsupported,
        matches: if (actual.ast == null) null else actual.ast == (parse(source, {type: "latex"})^)}
}
[for (source in [
    "\\begin{document}A\\the\\parshape B\\end{document}",
    "\\begin{document}A\\the\\clubpenalties1 B\\end{document}",
    "\\begin{document}A\\textbf{B\\the\\parshape C}D\\end{document}",
    "\\begin{document}$x+\\the\\clubpenalties1$\\end{document}",
    "\\def\\x{Hi}\\count0=\\parshape\\begin{document}A\\x B\\end{document}"])
    digestion(source)^]
