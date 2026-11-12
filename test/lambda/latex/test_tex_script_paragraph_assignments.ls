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
    "\\begin{document}A\\patterns{a1b}B\\end{document}",
    "\\begin{document}A\\hyphenation{hy-phen words}B\\end{document}",
    "\\begin{document}A\\patterns{{a{b}}c}B\\end{document}",
    "\\def\\t{\\message{no} x}\\begin{document}A\\patterns{\\t}B\\end{document}",
    "\\let\\open={\\def\\brace{\\open}\\begin{document}A\\patterns\\brace word}B\\end{document}",
    "\\let\\open={\\begin{document}A\\hyphenation\\open word}B\\end{document}",
    "\\begin{document}A\\parshape=1 1pt 2pt B\\end{document}",
    "\\begin{document}A\\parshape 2 1pt 2pt 3pt 4pt B\\end{document}",
    "\\begin{document}A\\parshape0 B\\end{document}",
    "\\begin{document}A\\parshape-1 B\\end{document}",
    "\\def\\n{2 }\\def\\d{1pt}\\begin{document}A\\parshape\\n \\d 2pt 3pt 4pt B\\end{document}",
    "\\dimen0=2pt\\begin{document}A\\parshape1 \\dimen0 \\dimexpr1pt+2pt\\relax B\\end{document}",
    "\\begin{document}A\\interlinepenalties=2 100 200 B\\end{document}",
    "\\begin{document}A\\clubpenalties1 -100 B\\end{document}",
    "\\begin{document}A\\widowpenalties0 B\\end{document}",
    "\\begin{document}A\\displaywidowpenalties2 1 2 B\\end{document}",
    "\\count0=12\\begin{document}A\\clubpenalties2 \\count0 \\numexpr1+2\\relax B\\end{document}",
    "\\let\\p=\\patterns\\begin{document}A\\p{a1b}B\\end{document}",
    "\\let\\p=\\parshape\\begin{document}A\\p1 1pt 2pt B\\end{document}",
    "\\begin{document}A\\global\\patterns{a1b}B\\global\\parshape1 1pt 2pt C\\end{document}",
    "\\begin{document}A\\afterassignment X\\patterns{a1b}B\\end{document}",
    "\\begin{document}A\\afterassignment X\\parshape1 1pt 2pt B\\end{document}",
    "\\begin{document}A\\afterassignment X\\global\\clubpenalties1 10 B\\end{document}",
    "\\def\\p{\\patterns{a1b}}\\begin{document}A\\p B\\end{document}",
    "\\begin{document}A{\\parshape1 1pt 2pt B}C\\end{document}"
];
[for (source in sources) matches(source)^]

fn digestion(source) any^ {
    let expanded = profile.expand_with_kernel(source, kernel,
        {echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let result = digest.digest(expanded)^;
    {issue: result.issue, unsupported: result.unsupported}
}
[for (source in [
    "\\def\\x{test}\\patterns{abc}\\begin{document}A\\x B\\end{document}",
    "\\begin{document}A\\textbf{B\\hyphenation{words}C}D\\end{document}",
    "\\begin{document}$x\\interlinepenalties1 10 y$\\end{document}"])
    digestion(source)^]
