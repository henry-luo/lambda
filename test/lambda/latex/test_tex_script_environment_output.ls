import profile: lambda.doc.latex.tex_engine.profile
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn render(token) => if (token.kind == "control") "\\" ++ token.value else token.value
fn matches(source) any^ {
    let expanded = profile.expand_with_kernel(source, kernel,
        {echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let text = join([for (token in expanded.output) render(token)], "");
    expanded.issue == null and text == (parse(source, {type: "tex"})^).text
}
let sources = [
    "\\def\\ename{quote}\\begin{document}\\begin{\\ename}A\\end{\\ename}\\end{document}",
    "\\def\\close{\\end{quote}}\\begin{document}\\begin{quote}A\\close\\end{document}",
    "\\def\\finish{\\end{document}}\\begin{document}A\\finish",
    "\\begin{document}\\begin{quote}A\\end{quote}\\end{document}",
    "\\AtEndDocument{Z}\\begin{document}A\\end{document}",
    "\\def\\alias{quote}\\def\\ename{\\alias}\\begin{document}\\begin{\\ename}A\\end{\\ename}\\end{document}"
];
[for (source in sources) matches(source)^]
