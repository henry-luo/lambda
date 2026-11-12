import utf8: lambda.io.utf8
import profile: lambda.doc.latex.tex_engine.profile
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
    {issue: result.issue, matches: text == (parse(source, {type: "tex"})^).text}
}
let sources = [
    "\\begin{document}A\\hskip1pt!B\\end{document}",
    "\\begin{document}A\\vskip1pt!B\\end{document}",
    "\\begin{document}A\\kern1pt!B\\end{document}",
    "\\begin{document}$a\\hskip1pt!b\\mskip2mu!c\\kern3pt!d\\mkern4mu!$\\end{document}",
    "\\begin{document}A\\hskip1pt plus2fil minus3pt!B\\end{document}",
    "\\def\\s{\\hskip1pt}\\begin{document}A\\s!B\\end{document}",
    "\\def\\k{\\kern1pt}\\begin{document}A\\k!B\\end{document}",
    "\\skip0=1pt plus2fil\\begin{document}A\\hskip\\skip0!B\\end{document}",
    "\\begin{document}A\\kern\\dimexpr1pt+2pt\\relax!B\\end{document}",
    "\\begin{document}A\\hskip1cm!B\\end{document}",
    "\\begin{document}A\\hskip1in!B\\end{document}",
    "\\begin{document}A\\kern-1.25pt!B\\end{document}",
    "\\font\\a=cmr10 at 10pt \\a\\begin{document}A\\kern1em!B\\end{document}",
    "\\pdfpxdimen=2pt \\begin{document}A\\kern1.5px!B\\end{document}",
    "\\begin{document}$a\\vskip1pt!b$\\end{document}",
    "\\def\\a{1}\\begin{document}A\\kern\\a pt!B\\end{document}",
    "\\begin{document}A\\kern1cm text\\end{document}",
    "\\begin{document}A\\kern 1cm!B\\end{document}",
    "\\begin{document}A\\hskip1cm plus2fil!B\\end{document}",
    "\\begin{document}é\\kern 1cm!B\\end{document}",
    "\\begin{document}A\\kern% note\n1cm!B\\end{document}",
    "\\let\\k=\\kern\\begin{document}A\\k1cm!B\\end{document}"
];
[for (source in sources) matches(source)^]
let long_source = "\\begin{document}A\\kern0." ++ join([for (i in 1 to 80) "0"], "") ++ "cm!B\\end{document}";
matches(long_source)^
