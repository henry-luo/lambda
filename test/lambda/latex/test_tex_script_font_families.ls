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
 "\\begin{document}A\\the\\hyphenchar\\textfont0 B\\end{document}",
 "\\begin{document}A\\the\\skewchar\\scriptfont2 B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\textfont0=\\f\\begin{document}A\\the\\hyphenchar\\textfont0 B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\hyphenchar\\f=63\\scriptfont2=\\f\\begin{document}A\\the\\hyphenchar\\scriptfont2 B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\skewchar\\f=-7\\scriptscriptfont3=\\f\\begin{document}A\\the\\skewchar\\scriptscriptfont3 B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\hyphenchar\\f=63\\textfont17=\\f\\begin{document}A\\the\\hyphenchar\\textfont1 B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\hyphenchar\\f=63\\textfont-1=\\f\\begin{document}A\\the\\hyphenchar\\textfont15 B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\hyphenchar\\f=63{\\textfont2=\\f}\\begin{document}A\\the\\hyphenchar\\textfont2 B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\hyphenchar\\f=63\\textfont1=\\f\\scriptfont3=\\textfont1\\begin{document}A\\the\\hyphenchar\\scriptfont3 B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\hyphenchar\\f=63\\f\\textfont1=\\font\\begin{document}A\\the\\hyphenchar\\textfont1 B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\textfont1=\\f\\hyphenchar\\textfont1=42\\begin{document}A\\the\\hyphenchar\\f B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\textfont1=\\f\\begin{document}A\\the\\fontdimen6\\textfont1 B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\hyphenchar\\f=63\\count0=3\\textfont\\count0=\\f\\begin{document}A\\the\\hyphenchar\\textfont3 B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\hyphenchar\\f=63\\let\\tf=\\textfont\\tf1=\\f\\begin{document}A\\the\\hyphenchar\\tf1 B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\hyphenchar\\f=63\\global\\textfont1=\\f\\begin{document}A\\the\\hyphenchar\\textfont1 B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\begin{document}A\\afterassignment X\\textfont1=\\f B\\end{document}"
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
    "\\begin{document}A\\the\\hyphenchar\\textfont0 B\\end{document}",
    "\\begin{document}A\\textbf{B\\the\\skewchar\\scriptfont1 C}D\\end{document}",
    "\\begin{document}A\\textbf{B\\textfont1=\\nullfont C}D\\end{document}",
    "\\def\\x{Hi}\\textfont1=\\nullfont\\begin{document}A\\x B\\end{document}"])
    digestion(source)^]
