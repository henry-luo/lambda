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
 "\\newenvironment{x}{before}{after}X",
 "\\newenvironment{x} {before}{after}X",
 "\\newenvironment{x}\n{before}\n{after}X",
 "\\newenvironment{x}  {before}  {after}X",
 "\\newenvironment {x}{before}{after}X",
 "\\newenvironment{x} [1] {before#1} {after}X",
 "\\newcommand{\\x} {abc}\\x",
 "\\newcommand {\\x}{abc}\\x",
 "\\newenvironment{x}{A} {B}X", "\\newenvironment{x}{A}\n{B}X",
 "\\newenvironment{x}[1] {A#1}{B}X", "\\newenvironment{x}[1]{A#1} {B}X",
 "\\newenvironment{x}[1][v] {A#1}{B}X", "\\newenvironment{x}[1][v]{A#1} {B}X",
 "\\newenvironment*{x} {A}{B}X", "\\newenvironment* {x}{A}{B}X",
 "\\newenvironment{x}{A}{B}\\renewenvironment{x} {C}{D}X",
 "\\newcommand{\\x}[1] {A#1}\\x{B}", "\\newcommand{\\x}[1][V] {A#1}\\x",
 "\\newenvironment{x}{A\\textbf{B}} {C}X",
 "\\newenvironment{x}%comment\n{A}{B}X", "\\newenvironment{x}{A}%comment\n{B}X",
 "\\newenvironment{x}\n\n{A}{B}X",
 "\\newenvironment%comment\n{x}{A}{B}X",
 "\\newenvironment \n{x}{A}{B}X",
 "\\newcommand{\\x}{}\\renewcommand{\\x} {B}\\x",
 "\\providecommand{\\x} {B}\\x",
 "\\newenvironment{x}[1][] {A#1}{B}X",
 "\\newenvironment{x}{} {}X"
]) check(body)^]
