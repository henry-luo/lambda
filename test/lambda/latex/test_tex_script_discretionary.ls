import profile: lambda.doc.latex.tex_engine.profile
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn render(token) => if (token.kind == "control") "\\" ++ token.value else token.value
fn check(source) any^ {
    let x = profile.expand_with_kernel(source, kernel,
        {echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let text = join([for (token in x.output) render(token)], "");
    {issue: x.issue, matches: text == (parse(source, {type: "tex"})^).text, messages: x.messages}
}
[for (source in [
    "\\begin{document}A\\discretionary{-}{z}{bc}D\\end{document}",
    "\\begin{document}A\\discretionary{}{}{}D\\end{document}",
    "\\def\\word{X}\\begin{document}A\\discretionary{\\word}{\\word}{\\word}D\\end{document}",
    "\\begin{document}A\\discretionary{\\message{PRE}}{\\message{POST}}{C}D\\end{document}",
    "\\begin{document}A\\discretionary{a{b}c}{d{e}f}{x{y}z}D\\end{document}",
    "\\let\\disc=\\discretionary\\begin{document}A\\disc{-}{z}{bc}D\\end{document}",
    "\\begin{document}A\\discretionary\\relax{-}\\relax{z}\\relax{bc}D\\end{document}",
    "\\begin{document}A\\discretionary{}{}{\\textbf{B}}D\\end{document}",
    "\\begin{document}A\\discretionary{}{}{\\number\\numexpr1+2\\relax}D\\end{document}",
    "\\let\\beg={\\begin{document}A\\discretionary\\beg -}{z}{bc}D\\end{document}",
    "\\begin{document}é\\discretionary{}{}{你}D\\end{document}",
    "\\count0=0 \\begin{document}A\\discretionary{\\advance\\count0 by 10}{}{\\advance\\count0 by 2}\\the\\count0\\end{document}",
    "\\begin{document}A\\discretionary{}{}{\\message{replacement}B}D\\end{document}"
]) check(source)^]
let bad = profile.expand_with_kernel("\\begin{document}\\discretionary X", kernel,
    {echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
bad.issue
