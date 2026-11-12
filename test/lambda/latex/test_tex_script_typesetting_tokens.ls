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
    "\\begin{document}\\noindent!\\end{document}",
    "\\begin{document}\\indent!\\end{document}",
    "\\begin{document}\\hfil!\\hfill!\\hss!\\hfilneg!\\end{document}",
    "\\begin{document}\\vfil!\\vfill!\\vss!\\vfilneg!\\end{document}",
    "\\begin{document}A\\penalty-100!\\unpenalty!\\unkern!B\\end{document}",
    "\\begin{document}\\noalign!\\omit!\\span!\\cr!\\crcr!\\end{document}",
    "\\begin{document}\\-!\\/!\\noboundary!\\end{document}",
    "\\let\\sp=\\hfil\\begin{document}\\sp!\\end{document}",
    "\\begin{document}A \\unskip!B\\end{document}",
    "\\begin{document}A\\unskip!B\\end{document}",
    "\\begin{document}A\\penalty\\numexpr2+3\\relax!B\\end{document}",
    "\\let\\p=\\penalty\\begin{document}A\\p-50!B\\end{document}",
    "\\begin{document}$\\mathord{a}\\mathop{b}\\mathbin{+}\\mathrel{=}\\mathopen{(}\\mathclose{)}\\mathpunct{,}\\mathinner{x}$\\end{document}",
    "\\begin{document}$\\displaystyle{a}\\textstyle{b}\\scriptstyle{c}\\scriptscriptstyle{d}$\\end{document}",
    "\\begin{document}$\\sum\\limits!\\nolimits!\\displaylimits!\\nonscript!\\eqno!\\leqno!\\mathchoice{a}{b}{c}{d}$\\end{document}",
    "\\begin{document}$\\left(a\\middle|b\\right)$\\end{document}",
    "\\begin{document}${a\\over{b}}{a\\atop{b}}{a\\above1pt{b}}$\\end{document}",
    "\\begin{document}$\\overwithdelims()!\\atopwithdelims()!\\abovewithdelims()!$\\end{document}"
];
[for (source in sources) matches(source)^]
