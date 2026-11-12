import utf8: lambda.io.utf8
import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
fn matches(source) any^ {
    let result = profile.expand_with_kernel(source, kernel,
        {script_expl3: true, echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let rebuilt = reconstruct.reconstruct(result)^;
    let native = parse(source, {type: "tex"})^;
    {issue: result.issue, reconstruct_issue: rebuilt.issue, matches: rebuilt.text == native.text,
        diagnostics_match: result.diagnostic_records == native.diagnostics, messages_empty: len(result.messages) == 0}
}
let sources = [
 "\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{A}\\tl_put_right:Nn\\l_test_tl{B}\\tl_use:N\\l_test_tl\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{A}\\tl_put_left:Nn\\l_test_tl{B}\\tl_use:N\\l_test_tl\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_put_right:Nn\\l_test_tl{A}\\tl_put_left:Nn\\l_test_tl{B}\\tl_use:N\\l_test_tl\\ExplSyntaxOff",
 "\\def\\x{Q}\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_put_right:Nx\\l_test_tl{\\x}\\def\\x{Z}\\tl_use:N\\l_test_tl\\ExplSyntaxOff",
 "\\def\\x{Q}\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_put_left:Nx\\l_test_tl{\\x}\\def\\x{Z}\\tl_use:N\\l_test_tl\\ExplSyntaxOff",
 "\\def\\x{Q}\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_put_right:Nn\\l_test_tl{\\x}\\def\\x{Z}\\tl_use:N\\l_test_tl\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{A}{\\tl_put_right:Nn\\l_test_tl{B}\\tl_use:N\\l_test_tl}\\tl_use:N\\l_test_tl\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{A}{\\tl_gput_right:Nn\\l_test_tl{B}}\\tl_use:N\\l_test_tl\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_set:Nn\\l_test_tl{A}{\\tl_gput_left:Nn\\l_test_tl{B}}\\tl_use:N\\l_test_tl\\ExplSyntaxOff",
 "\\def\\x{Q}\\ExplSyntaxOn\\tl_new:N\\l_test_tl{\\tl_gput_right:Nx\\l_test_tl{\\x}}\\def\\x{Z}\\tl_use:N\\l_test_tl\\ExplSyntaxOff",
 "\\def\\x{Q}\\ExplSyntaxOn\\tl_new:N\\l_test_tl{\\tl_gput_left:Nx\\l_test_tl{\\x}}\\def\\x{Z}\\tl_use:N\\l_test_tl\\ExplSyntaxOff",
 "\\ExplSyntaxOn[\\tl_use:N\\l_tmpa_tl][\\tl_use:N\\l_tmpb_tl][\\tl_use:N\\g_tmpa_tl][\\tl_use:N\\g_tmpb_tl]\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_put_right:Nn\\l_tmpa_tl{A}\\tl_put_left:Nn\\l_tmpa_tl{B}\\tl_use:N\\l_tmpa_tl\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_put_right:Nn\\l_tmpa_tl{{A}~B}\\tl_use:N\\l_tmpa_tl\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_put_right:Nn\\l_tmpa_tl{é藏}\\tl_use:N\\l_tmpa_tl\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_put_right:Nn\\g_tmpa_tl{A}{\\tl_gput_right:Nn\\g_tmpa_tl{B}}\\tl_use:N\\g_tmpa_tl\\ExplSyntaxOff",
 "\\globaldefs=1\\ExplSyntaxOn{\\tl_put_right:Nn\\l_tmpa_tl{A}}\\tl_use:N\\l_tmpa_tl\\ExplSyntaxOff\\globaldefs=0",
 "\\globaldefs=-1\\ExplSyntaxOn{\\tl_gput_right:Nn\\g_tmpa_tl{A}}[\\tl_use:N\\g_tmpa_tl]\\ExplSyntaxOff\\globaldefs=0",
 "\\ExplSyntaxOn\\afterassignment X\\tl_put_right:Nn\\l_tmpa_tl{A}\\tl_use:N\\l_tmpa_tl\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{A}\\tl_put_right:Nx\\l_tmpa_tl{\\tl_use:N\\l_tmpa_tl B}\\tl_use:N\\l_tmpa_tl\\ExplSyntaxOff"
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
    "\\ExplSyntaxOn\\tl_put_right:Nn\\l_tmpa_tl{A}\\ExplSyntaxOff\\begin{document}Hello\\end{document}",
    "\\ExplSyntaxOn\\begin{document}\\textbf{\\tl_put_right:Nn\\l_tmpa_tl{A}B}\\end{document}",
    "\\def\\x{Hi}\\ExplSyntaxOn\\tl_put_right:Nn\\l_tmpa_tl{A}\\ExplSyntaxOff\\begin{document}\\x\\end{document}"])
    safe_digestion(source)^]
