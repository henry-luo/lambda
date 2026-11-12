import profile: lambda.doc.latex.tex_engine.profile
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn render(token) => if (token.kind == "control") "\\" ++ token.value else token.value
fn matches(source) any^ {
    let expanded = profile.expand_with_kernel(source, kernel,
        {echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let text = join([for (token in expanded.output) render(token)], "");
    expanded.issue == null and len(expanded.messages) == 0 and text == (parse(source, {type: "tex"})^).text
}
let sources = [
    "\\begin{document}A\\special{color push}B\\end{document}",
    "\\begin{document}A\\mark{abc}B\\end{document}",
    "\\begin{document}A\\marks12{abc}B\\end{document}",
    "\\begin{document}\\topmark!\\firstmark!\\botmark!\\splitfirstmark!\\splitbotmark!\\end{document}",
    "\\begin{document}\\topmarks1!\\firstmarks2!\\botmarks3!\\splitfirstmarks4!\\splitbotmarks5!\\end{document}",
    "\\begin{document}A\\marks\\numexpr2+3\\relax{abc}B\\end{document}",
    "\\def\\x{abc}\\begin{document}A\\special{a{\\x}b}B\\end{document}",
    "\\begin{document}A\\special{\\message{hidden}}B\\end{document}",
    "\\let\\m=\\mark\\begin{document}A\\m{abc}B\\end{document}",
    "\\begin{document}A\\topmarks\\numexpr1+2\\relax!B\\end{document}",
    "\\edef\\x{A\\topmark B}\\begin{document}\\x!\\end{document}",
    "\\begin{document}A\\marks-1{a{b}c}B\\end{document}"
];
[for (source in sources) matches(source)^]
