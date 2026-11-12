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
 "\\begin{document}A\\the\\hyphenchar\\nullfont B\\end{document}",
 "\\begin{document}A\\the\\skewchar\\nullfont B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\begin{document}A\\the\\hyphenchar\\f,\\the\\skewchar\\f B\\end{document}",
 "\\defaulthyphenchar=43\\defaultskewchar=44\\font\\f=cmr10 at 13pt\\begin{document}A\\the\\hyphenchar\\f,\\the\\skewchar\\f B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\hyphenchar\\f=63\\skewchar\\f=42\\begin{document}A\\the\\hyphenchar\\f,\\the\\skewchar\\f B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\font\\g=cmr10 at 13pt\\hyphenchar\\f=63\\begin{document}\\the\\hyphenchar\\g\\end{document}",
 "\\font\\f=cmr10 at 13pt{\\hyphenchar\\f=63}\\begin{document}\\the\\hyphenchar\\f\\end{document}",
 "\\font\\f=cmr10 at 13pt\\global\\skewchar\\f=-13\\begin{document}\\the\\skewchar\\f\\end{document}",
 "\\hyphenchar\\nullfont=30\\begin{document}\\the\\hyphenchar\\nullfont\\end{document}",
 "\\skewchar\\nullfont=-2\\begin{document}\\the\\skewchar\\nullfont\\end{document}",
 "\\font\\f=cmr10 at 13pt\\f\\hyphenchar\\font=53\\begin{document}\\the\\hyphenchar\\f\\end{document}",
 "\\font\\f=cmr10 at 13pt\\let\\g=\\f\\skewchar\\g=31\\begin{document}\\the\\skewchar\\f\\end{document}",
 "\\font\\f=cmr10 at 13pt\\let\\h=\\hyphenchar\\h\\f=55\\begin{document}\\the\\h\\f\\end{document}",
 "\\font\\f=cmr10 at 13pt\\begin{document}\\the\\numexpr\\hyphenchar\\f+1\\relax\\end{document}",
 "\\font\\f=cmr10 at 13pt\\count0=\\skewchar\\f\\begin{document}\\the\\count0\\end{document}",
 "\\font\\f=cmr10 at 13pt\\hyphenchar\\f=63\\begin{document}\\ifnum\\hyphenchar\\f=63 T\\else F\\fi\\end{document}",
 "\\font\\f=cmr10 at 13pt\\hyphenchar\\f=63\\dimen0=\\hyphenchar\\f pt\\begin{document}\\the\\dimen0\\end{document}",
 "\\font\\f=cmr10 at 13pt\\begin{document}A\\afterassignment X\\hyphenchar\\f=63 B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\begin{document}A\\afterassignment X\\global\\skewchar\\f=7 B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\def\\n{65 }\\hyphenchar\\f=\\n\\begin{document}\\the\\hyphenchar\\f\\end{document}"
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
    "\\begin{document}A\\the\\hyphenchar\\nullfont B\\end{document}",
    "\\begin{document}A\\textbf{B\\the\\skewchar\\nullfont C}D\\end{document}",
    "\\begin{document}$x+\\the\\hyphenchar\\nullfont$\\end{document}",
    "\\begin{document}A\\textbf{B\\hyphenchar\\nullfont=44 C}D\\end{document}",
    "\\def\\x{Hi}\\hyphenchar\\nullfont=44\\begin{document}A\\x B\\end{document}"])
    digestion(source)^]
