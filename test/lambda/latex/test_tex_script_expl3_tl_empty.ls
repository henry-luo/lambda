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
 "\\ExplSyntaxOn\\tl_if_empty:NTF\\l_tmpa_tl{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{A}\\tl_if_empty:NTF\\l_tmpa_tl{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_empty:NT\\l_tmpa_tl{T}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{A}\\tl_if_empty:NT\\l_tmpa_tl{T}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_empty:NF\\l_tmpa_tl{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{A}\\tl_if_empty:NF\\l_tmpa_tl{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_empty:nTF{}{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_empty:nTF{A}{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_empty:nTF{~}{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_empty:nTF{{}}{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_empty:nT{}{T}\\tl_if_empty:nF{A}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_empty:nT{A}{T}\\tl_if_empty:nF{}{F}\\ExplSyntaxOff",
 "\\def\\x{}\\ExplSyntaxOn\\tl_if_empty:nTF{\\x}{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{~}\\tl_if_empty:NTF\\l_tmpa_tl{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_empty:NTF\\l_tmpa_tl{T}{\\seq_pop_left:NN\\l_missing_seq\\l_tmpb_tl}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{A}\\tl_if_empty:NTF\\l_tmpa_tl{\\seq_pop_left:NN\\l_missing_seq\\l_tmpb_tl}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_new:N\\l_test_tl{\\tl_put_right:Nn\\l_test_tl{A}\\tl_if_empty:NTF\\l_test_tl{T}{F}}\\tl_if_empty:NTF\\l_test_tl{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\x{\\tl_if_empty:NTF\\l_tmpa_tl{T}{F}}\\x\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_new:N\\l_test_tl\\tl_put_right:Nn\\l_test_tl{A}\\tl_clear:N\\l_test_tl\\tl_if_empty:NTF\\l_test_tl{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_empty:nTF{é}{T}{F}\\ExplSyntaxOff",
 "\\def\\foo{}\\ExplSyntaxOn\\tl_if_empty:NTF\\foo{T}{F}\\ExplSyntaxOff",
 "\\long\\def\\foo{}\\ExplSyntaxOn\\tl_if_empty:NTF\\foo{T}{F}\\ExplSyntaxOff",
 "\\protected\\def\\foo{}\\ExplSyntaxOn\\tl_if_empty:NTF\\foo{T}{F}\\ExplSyntaxOff",
 "\\def\\foo X{}\\ExplSyntaxOn\\tl_if_empty:NTF\\foo{T}{F}\\ExplSyntaxOff",
 "\\let\\foo=\\relax\\ExplSyntaxOn\\tl_if_empty:NTF\\foo{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_empty:NTF\\l_missing_tl{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_empty:NTF a{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\x{\\tl_if_empty:nTF{A}{T}{F}}\\x\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\x{\\tl_if_empty:NTF\\l_tmpa_tl{}{F}}[\\x]\\ExplSyntaxOff",
 "\\def\\foo{Q}\\ExplSyntaxOn\\edef\\x{\\tl_if_empty:nTF{}{\\foo}{F}}\\x\\ExplSyntaxOff",
 "\\def\\foo{Q}\\ExplSyntaxOn\\edef\\x{\\tl_if_empty:nTF{}{\\noexpand\\foo}{F}}\\def\\foo{Z}\\x\\ExplSyntaxOff"
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
    "\\ExplSyntaxOn\\begin{document}\\tl_if_empty:NTF\\l_tmpa_tl{T}{F}\\end{document}",
    "\\ExplSyntaxOn\\begin{document}\\textbf{\\tl_if_empty:NTF\\l_tmpa_tl{T}{F}}\\end{document}",
    "\\def\\x{Hi}\\ExplSyntaxOn\\tl_if_empty:NTF\\l_tmpa_tl{T}{F}\\ExplSyntaxOff\\begin{document}\\x\\end{document}"])
    safe_digestion(source)^]
