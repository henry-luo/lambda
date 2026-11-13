import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn check(body) any^ {
    let source = "\\begin{document}" ++ body ++ "\\end{document}";
    let expanded = profile.expand_with_kernel(source, kernel, {constructor_argument_mode: true, echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let result = digest.digest(expanded)^;
    let native = parse(source, {type: "latex"}) ^ { null };
    {body: body, issue: result.issue, unsupported: result.unsupported,
        matches: if (result.ast == null or native == null) null else result.ast == native,
        actual: if (result.ast == null or native == null or result.ast == native) null else result.ast,
        native: if (result.ast == null or native == null or result.ast == native) null else native}
}
[for (body in [
 "\\newenvironment{x}\n{\\renewcommand{\\foo}{A}}\n{B}X",
 "\\newenvironment{x}\n{\\renewcommand{\\foo}{A} \\small}\n{B}X",
 "\\newenvironment{x}\n{\\renewcommand{\\foo}{A}}\n{\\newcommand{\\bar}{B}}X",
 "\\newenvironment{x}\n{\\def\\foo{A} \\foo}\n{B}X",
 "\\newenvironment{x}\n{\\renewcommand{\\foo} {A}}\n{B}X",
 "\\newenvironment{x}\n{\\renewcommand{\\foo}[1]{#1} \\textbf{T}}\n{B}X",
 "\\newenvironment{x}\n{\\newcommand*{\\foo}{A}}\n{B}X",
 "\\newenvironment{x}\n{\\newcommand{\\foo}[1]{##1} \\small}\n{B}X",
 "\\newenvironment{x}\n{\\def\\foo#1{#1} \\small}\n{B}X",
 "\\newenvironment{x}\n{\\def\\foo{A}\\def\\bar{B}}\n{B}X",
 "\\newenvironment{x}\n{A\\newcommand{\\foo}{A}B}\n{B}X",
 "\\newenvironment{x}\n{\\newenvironment{y}{A}{B} \\small}\n{B}X",
 "\\newenvironment{x}\n{\\renewcommand{\\foo}{A}%comment\n\\small}\n{B}X",
 "\\newenvironment{x}\n{\\renewcommand{\\foo}{\\textbf{A}} \\small}\n{B}X",
 "\\newcommand{\\foo}{\\renewcommand{\\bar}{A}}X",
 "\\newcommand{\\foo}{\\def\\bar{A} \\small}X",
 "\\newenvironment{x}\n{\\global\\def\\foo{A} \\small}\n{B}X",
 "\\newenvironment{x}\n{\\newcommand{\\foo}{A}  {B}}\n{B}X",
 "\\catcode`@=11 \\newenvironment{x}\n{\\def\\foo@bar{A} \\small}\n{B}X",
 "\\newenvironment{x}[1]\n{\\def\\foo{#1} \\small}\n{B}X",
 "\\newenvironment{x}\n{\\def\\foo{\\newcommand{\\bar}{B}} \\small}\n{B}X",
 "\\newenvironment{x}\n{\\newcommand{\\foo}{A}\\newcommand{\\bar}{B}C}\n{B}X",
 "\\newenvironment{x}\n{\\def\\foo{} \\small}\n{B}X",
 "\\newenvironment{x}\n{{\\def\\foo{A}} \\small}\n{B}X",
 "\\newenvironment{x}\n{\\renewcommand{\\foo}[1][V]{##1} \\small}\n{B}X"
]) check(body)^]
