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
 "\\ExplSyntaxOn\\tl_count:n{}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_count:n{ABC}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_count:n{~A~~B~}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_count:n{{AB}C}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_count:n{{}{}}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_count:n{{{A}B}C}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_count:n{\\foo}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_count:n{\\foo\\bar}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_count:n{\\foo{A}}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_count:n{éα}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_count:n{#}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_count:n{^_&$}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_count:N\\l_tmpa_tl\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{ABC}\\tl_count:N\\l_tmpa_tl\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{A{BC}~D}\\tl_count:N\\l_tmpa_tl\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\def\\foo{ABC}\\tl_count:N\\foo\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\long\\def\\foo{ABC}\\tl_count:N\\foo\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\protected\\def\\foo{ABC}\\tl_count:N\\foo\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\def\\foo{ABC}\\tl_count:n{\\foo}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\x{\\tl_count:n{ABC}}\\x\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\x{\\tl_count:n{}}\\x\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{ABC}\\edef\\x{\\tl_count:N\\l_tmpa_tl}\\x\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\x{\\tl_count:N\\l_tmpa_tl}\\x\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{A}{\\tl_set:Nn\\l_tmpa_tl{BC}\\tl_count:N\\l_tmpa_tl}\\tl_count:N\\l_tmpa_tl\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_count:n{{~~}~{A~B}}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_count:n{{\\foo{A}}{\\bar}}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_count:n{\\foo~{A}}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_count:n{{A}~{B}}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_count:n{1234567890}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\x{\\tl_count:n{AB}}\\edef\\y{\\x}\\y\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_count:n{~}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_count:n{\\par}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_count:n{{\\foo~A}B~{C}}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{\\foo{A}~}\\tl_count:N\\l_tmpa_tl\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_count:n{AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_count:n{AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\x{[\\tl_count:n{{A}~B}]}\\edef\\y{\\x}\\y\\ExplSyntaxOff"
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
    "\\ExplSyntaxOn\\begin{document}\\tl_count:n{ABC}\\end{document}",
    "\\ExplSyntaxOn\\begin{document}\\textbf{\\tl_count:N\\l_tmpa_tl}\\end{document}",
    "\\def\\x{Hi}\\ExplSyntaxOn\\tl_count:n{A}\\ExplSyntaxOff\\begin{document}\\x\\end{document}"])
    safe_digestion(source)^]
