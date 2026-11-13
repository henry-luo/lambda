import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn check(source) any^ {
    let expanded = profile.expand_with_kernel(source, kernel,
        {echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let built = reconstruct.reconstruct(expanded)^;
    let native = parse(source, {type: "tex"})^;
    {issue: expanded.issue, reconstruction_issue: built.issue,
        text_match: if (built.issue == null) built.text == native.text else null,
        diagnostics_match: expanded.diagnostic_records == native.diagnostics,
        actual: if (built.text == native.text) null else built.text,
        native: if (built.text == native.text) null else native.text}
}
[for (source in [
    "\\def\\foo{EXPANDED}\\begin{document}\\directlua{\\foo}Z\\end{document}",
    "\\def\\foo{EXPANDED}\\begin{document}\\luaexec{\\foo}Z\\end{document}",
    "\\begin{document}\\directlua {A {B} C}Z\\end{document}",
    "\\begin{document}\\directlua\t{A \\} B}Z\\end{document}",
    "\\begin{document}\\directlua{A% } ignored\n B}Z\\end{document}",
    "\\begin{document}\\directlua{A\nB\n{C}}Z\\end{document}",
    "\\begin{document}é\\directlua{λ}B\\end{document}",
    "\\begin{document}\\directlua{A\r\nB}Z\\end{document}",
    "\\begin{document}\\directlua{}\\directlua{B}Z\\end{document}",
    "\\def\\luaexec#1{EXPANDED}\\begin{document}\\luaexec{X}\\end{document}",
    "\\def\\foo{\\directlua{ABC}}\\begin{document}\\foo\\end{document}",
    "\\directlua{raw}",
    "\\directlua{raw",
    "\\directlua",
    "\\begin{document}\\directlua  X\\end{document}",
    "\\begin{document}\\directlua{A\\{B\\} C}D\\end{document}",
    "\\begin{document}\\directlua{A% unclosed }\r\n B}C\\end{document}",
    "\\begin{document}\\directlua{é\nλ}\n\\textbf{B}\\end{document}",
    "\\begin{document}\\directlua{A\nB}\\char-1\\end{document}",
    "\\begin{document}\\directlua{A\nB}\n\\char-1\\end{document}"
]) check(source)^]

let root = "test/lambda/latex/fixtures/local_packages/";
let source = input(root ++ "raw_commands_main.tex", "text")^;
let package = input(root ++ "raw_commands.sty", "text")^;
let expanded = profile.expand_with_kernel(source, kernel, {echo_definitions: true, adapters: [],
    source_name: root ++ "raw_commands_main.tex", resources: {local: {'raw_commands.sty': {data: package}}, bundled: {}}}, 100000)^;
let built = reconstruct.reconstruct(expanded)^;
let native = input(root ++ "raw_commands_main.tex", "tex")^;
{issue: expanded.issue, reconstruction_issue: built.issue,
    text_match: if (built.issue == null) built.text == native.text else null,
    diagnostics_match: expanded.diagnostic_records == native.diagnostics,
    actual: if (built.text == native.text) null else built.text,
    native: if (built.text == native.text) null else native.text}

let raw = profile.expand_with_kernel("\\begin{document}\\directlua{code}\\end{document}", kernel,
    {resources: {local: {}, bundled: {}}}, 100000)^;
let pending = digest.digest(raw)^;
{digestion_guarded: pending.ast == null and pending.issue == "latex-digest-unsupported" and
    pending.unsupported == "raw inline source mapping"}
