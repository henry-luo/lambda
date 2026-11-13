import digest: lambda.doc.latex.tex_engine.digest
import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
let native_preload_messages = parse("", {type: "tex"})^.messages;
fn check(source) any^ {
    let x = profile.expand_with_kernel(source, kernel, {echo_definitions: true,
        adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let r = reconstruct.reconstruct(x)^;
    let n = parse(source, {type: "tex"})^;
    {source: source, expansion_issue: x.issue, reconstruction_issue: r.issue,
        text_match: if (r.issue == null) r.text == n.text else null,
        document_messages_match: x.messages == slice(n.messages, len(native_preload_messages), len(n.messages))^, diagnostics_match: x.diagnostic_records == n.diagnostics,
        actual: if (r.issue != null or r.text == n.text) null else r.text,
        native: if (r.issue != null or r.text == n.text) null else n.text}
}
[for (source in [
    "\\def\\q#1{#1}\\q{A}",
    "\\def\\q#1{#1}\\q{A  B}",
    "\\def\\q#1{#1#1}\\q{A  B}",
    "\\def\\q#1{[#1]}\\q{é猫}",
    "\\def\\q#1{#1}\\q{A% comment\nB}",
    "\\def\\q#1{#1}\\q{{A} B}",
    "\\def\\q#1#2{#2/#1}\\q{A}{B}",
    "\\def\\q#1,#2;{#2/#1}\\q A  B,C;",
    "\\def\\q#1{\\quad#1}\\q{B}",
    "\\def\\q#1{\\quad#1}\\q{\\textbf{B}}",
    "\\def\\q#1{\\quad#1#1}\\q{\\textbf{B}}",
    "\\def\\q#1{\\def\\w{#1}\\w}\\q{A  B}",
    "\\def\\a#1{#1}\\def\\b#1{\\a{#1}}\\b{A  B}",
    "\\def\\q#1{#1}\\q{\\relax A}",
    "\\def\\q#1{#1}\\q{\\string\\test}",
    "\\def\\q#1{\\textbf{#1}}\\begin{document}\\q{A B}\\end{document}",
    "\\def\\q#1{\\textbf{#1}}\\begin{document}\\q{\\emph{A}}\\end{document}",
    "\\newcommand{\\q}[2][Default]{#1/#2}\\q{B}",
    "\\newcommand{\\q}[2][Default]{#1/#2}\\q[Actual]{B}",
    "\\newcommand{\\q}[2][A B]{#1/#2}\\q{C}",
    "\\newcommand{\\q}[2][A B]{#1/#2}\\q[X Y]{C}",
    "\\newcommand{\\q}[2][]{#1/#2}\\q{C}",
    "\\newcommand{\\q}[2][A]{#1/#2}\\q[]{C}",
    "\\def\\q#1{#1}\\futurelet\\n\\q{A}B",
    "\\def\\q#1{#1}\\expandafter\\q\\expandafter{A}B",
    "\\def\\q#1{\\uppercase{#1}}\\q{abc}",
    "\\def\\q#1{#1}\\iftrue\\q{A B}\\fi",
    "\\def\\q#1{#1}\\q{}X",
    "\\def\\q#1{\\message{#1}}\\q{A B}",
    "\\def\\q#1{\\message{[#1]}}\\q{\\string\\foo}",
    "\\newcommand{\\q}[2][Hi]{\\message{#1/#2}}\\q{B}",
    "\\newcommand{\\q}[2][Hi]{\\message{#1/#2}}\\q[X]{B}"
]) check(source)^]

fn check_ast(source) any^ {
    let x = profile.expand_with_kernel(source, kernel, {echo_definitions: true,
        adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let result = digest.digest(x)^;
    let native = parse(source, {type: "latex"})^;
    {issue: result.issue, matches: if (result.ast == null) null else result.ast == native}
}
[for (source in [
    "\\def\\q#1{\\quad#1}\\begin{document}\\q{\\textbf{B}}C\\end{document}",
    "\\def\\q#1{\\quad#1}\\begin{document}\\q{\\emph{B}}C\\end{document}",
    "\\def\\q#1{#1#1}\\begin{document}\\q{\\textbf{B}}\\end{document}",
    "\\newcommand{\\q}[2][Z]{#1/#2}\\begin{document}\\q[\\textbf{A}]{B}\\end{document}"
]) check_ast(source)^]

let fixture_root = "test/lambda/latex/fixtures/local_packages/";
let fixture_source = input(fixture_root ++ "macro_origin_main.tex", "text")^;
let fixture_child = input(fixture_root ++ "macro_origin_child.tex", "text")^;
let fixture = profile.expand_with_kernel(fixture_source, kernel, {echo_definitions: true, adapters: [],
    source_name: fixture_root ++ "macro_origin_main.tex", resources: {local: {
        'macro_origin_child.tex': {data: fixture_child}}, bundled: {}}}, 100000)^;
let fixture_rebuilt = reconstruct.reconstruct(fixture)^;
let fixture_native = input(fixture_root ++ "macro_origin_main.tex", "tex")^;
let fixture_commands = [for (token in fixture.output where token.kind == "control" and token.value == "textbf") token];
{expansion_issue: fixture.issue, reconstruction_issue: fixture_rebuilt.issue,
    text_match: fixture_rebuilt.text == fixture_native.text,
    document_messages_match: fixture.messages == slice(fixture_native.messages, len(native_preload_messages), len(fixture_native.messages))^,
    diagnostics_match: fixture.diagnostic_records == fixture_native.diagnostics,
    argument_is_direct: len(fixture_commands) == 1 and fixture_commands[0].direct == true and
        fixture_commands[0].file == fixture.main_file}
