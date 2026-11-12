import profile: lambda.doc.latex.tex_engine.profile
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
fn render(token) => if (token.kind == "control") "\\" ++ token.value else token.value
fn check(source) any^ {
 let x=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let text=join([for (t in x.output) render(t)], "");
 let native=parse(source,{type:"tex"})^;
 {issue:x.issue,matches:text==native.text,diagnostics_match:x.diagnostic_records==native.diagnostics}
}
[for (n in [18,19,20,21,22,23,24,94,95,125,126,127,1])
    check("\\begin{document}\\accent" ++ string(n) ++ " e!\\end{document}")^];
[for (source in [
    "\\begin{document}\\accent19 \\char101!\\end{document}",
    "\\chardef\\letter=101 \\begin{document}\\accent19 \\letter!\\end{document}",
    "\\def\\letter{e}\\begin{document}\\accent19 \\letter!\\end{document}",
    "\\begin{document}\\accent19 \\relax!\\end{document}",
    "\\begin{document}\\accent19 é!\\end{document}",
    "\\begin{document}\\accent19 \\char123!\\end{document}",
    "\\begin{document}\\accent19 \\char92!\\end{document}",
    "\\let\\a=\\accent\\begin{document}\\a19 e!\\end{document}",
    "\\begin{document}\\accent19 \\char\\numexpr100+1\\relax!\\end{document}",
    "\\begin{document}\\accent-1 e!\\end{document}",
    "\\begin{document}\\accent1114112 e!\\end{document}",
    "\\begin{document}\\accent55296 e!\\end{document}",
    "\\begin{document}\\accent19 \\end{document}"
]) check(source)^]
