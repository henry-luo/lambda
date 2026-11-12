import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
fn matches(source) any^ {
    let result = profile.expand_with_kernel(source, kernel,
        {script_expl3: true, echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let rebuilt = reconstruct.reconstruct(result)^;
    let native = parse(source, {type: "tex"})^;
    {issue: result.issue, reconstruct_issue: rebuilt.issue, matches: rebuilt.text == native.text,
        diagnostics_match: result.diagnostic_records == native.diagnostics, messages_empty: len(result.messages) == 0}
}
let sources = [
 "\\ExplSyntaxOn\\tl_to_str:n{}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_to_str:n{hello}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_to_str:n{hello~world}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_to_str:n{\\foo}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_to_str:n{\\foo{A}~B}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_to_str:n{{A}}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_to_str:n{\\%}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_to_str:n{\\#}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_to_str:n{é}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{hello}\\tl_to_str:V\\l_tmpa_tl\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_to_str:V\\l_tmpa_tl\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{\\foo{A}~B}\\tl_to_str:V\\l_tmpa_tl\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\def\\foo{Z}\\tl_to_str:n{\\foo}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\def\\foo{Z}\\tl_set:Nn\\l_tmpa_tl{\\foo}\\tl_to_str:V\\l_tmpa_tl\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\x{\\tl_to_str:n{A}}\\x\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\x{\\tl_to_str:n{\\foo}}\\x\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{ABC}\\edef\\x{\\tl_to_str:V\\l_tmpa_tl}\\x\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\escapechar=-1\\tl_to_str:n{\\foo}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\escapechar=43\\tl_to_str:n{\\foo}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_to_str:n{\\foo}bar\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{~}\\tl_to_str:V\\l_tmpa_tl\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_to_str:n{#}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_to_str:n{##}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_to_str:n{^_&$}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_to_str:n{\\\\}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_to_str:n{\\a\\b}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\x{\\tl_to_str:n{A~B}}\\x\\ExplSyntaxOff",
 "\\def\\a{\\tl_to_str:n{\\foo}}\\ExplSyntaxOn\\edef\\x{\\a}\\x\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{\\foo}\\edef\\x{\\tl_to_str:V\\l_tmpa_tl}\\x\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_to_str:n{~}\\tl_to_str:n{~~}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{\\foo}\\tl_put_right:Nn\\l_tmpa_tl{{a}}\\tl_to_str:V\\l_tmpa_tl\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\def\\a{\\tl_to_str:n{\\foo}}\\edef\\x{\\a}\\x\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\def\\foo{AA}\\tl_to_str:V\\foo\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\long\\def\\foo{AA}\\tl_to_str:V\\foo\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\protected\\def\\foo{AA}\\tl_to_str:V\\foo\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{X}{\\tl_set:Nn\\l_tmpa_tl{Y}\\tl_to_str:V\\l_tmpa_tl}\\tl_to_str:V\\l_tmpa_tl\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\x{\\tl_to_str:n{}}[\\x]\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{}\\edef\\x{\\tl_to_str:V\\l_tmpa_tl}[\\x]\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\x{\\tl_to_str:n{A}}\\edef\\y{\\x}\\y\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\x{\\tl_to_str:n{\\foo}}\\edef\\y{\\x}\\y\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\x{\\tl_to_str:n{A}}\\edef\\y{[\\x]}\\y\\ExplSyntaxOff"
];
[for (source in sources) matches(source)^]

fn safe_digestion(source) any^ {
    let result = profile.expand_with_kernel(source, kernel,
        {script_expl3: true, echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let parsed = digest.digest(result)^;
    if (parsed.ast != null) parsed.ast == (parse(source, {type: "latex"})^)
    else parsed.issue == "latex-digest-unsupported"
}
[for (source in [
    "\\ExplSyntaxOn\\begin{document}\\tl_to_str:n{\\foo}\\end{document}",
    "\\ExplSyntaxOn\\begin{document}\\textbf{\\tl_to_str:V\\l_tmpa_tl}\\end{document}",
    "\\def\\x{Hi}\\ExplSyntaxOn\\tl_to_str:n{A}\\ExplSyntaxOff\\begin{document}\\x\\end{document}"])
    safe_digestion(source)^]
