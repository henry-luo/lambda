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
 "\\ExplSyntaxOn\\tl_if_eq:NNTF\\l_tmpa_tl\\l_tmpb_tl{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{A}\\tl_if_eq:NNTF\\l_tmpa_tl\\l_tmpb_tl{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{A}\\tl_set:Nn\\l_tmpb_tl{A}\\tl_if_eq:NNTF\\l_tmpa_tl\\l_tmpb_tl{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_eq:NNT\\l_tmpa_tl\\l_tmpb_tl{T}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_eq:NNF\\l_tmpa_tl\\l_tmpb_tl{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{A}\\tl_if_eq:NNF\\l_tmpa_tl\\l_tmpb_tl{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_eq:NNTF\\l_missing_a_tl\\l_missing_b_tl{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_eq:NNTF aa{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_eq:NNTF ab{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_eq:nnTF{}{}{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_eq:nnTF{A}{A}{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_eq:nnTF{A}{B}{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_eq:nnTF{{A}}{A}{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_eq:nnTF{{A}}{{A}}{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_eq:nnT{A}{A}{T}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_eq:nnF{A}{B}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_eq:nnTF{~}{}{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_eq:nnTF{\\foo}{\\foo}{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_eq:nnTF{\\foo}{\\bar}{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\z{\\tl_if_eq:nnTF{A}{A}{T}{F}}\\z\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\z{\\tl_if_eq:NNTF\\l_tmpa_tl\\l_tmpb_tl{T}{F}}\\z\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_eq:nnTF{é}{é}{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\def\\foo{A}\\tl_if_eq:nnTF{\\foo}{A}{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_eq:nnTF{A}{A}{\\tl_if_empty:nTF{}{T}{F}}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\def\\a{}\\long\\def\\b{}\\tl_if_eq:NNTF\\a\\b{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\z{\\tl_if_eq:nnTF{A}{B}{T}{F}}\\z\\ExplSyntaxOff",
 "\\def\\foo{Q}\\ExplSyntaxOn\\edef\\z{\\tl_if_eq:nnTF{A}{A}{\\foo}{F}}\\z\\ExplSyntaxOff",
 "\\def\\foo{Q}\\ExplSyntaxOn\\edef\\z{\\tl_if_eq:nnTF{A}{A}{\\noexpand\\foo}{F}}\\def\\foo{Z}\\z\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\z{\\tl_if_eq:nnT{A}{A}{T}}\\z\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\z{\\tl_if_eq:nnF{A}{B}{F}}\\z\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\z{\\tl_if_eq:NNTF\\l_tmpa_tl\\l_tmpb_tl{}{F}}[\\z]\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{A}\\edef\\z{\\tl_if_eq:NNTF\\l_tmpa_tl\\l_tmpb_tl{T}{F}}\\z\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_eq:nnTF{A}{A}{T}{\\seq_pop_left:NN\\l_missing_seq\\l_tmpb_tl}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_eq:nnTF{A}{B}{\\seq_pop_left:NN\\l_missing_seq\\l_tmpb_tl}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{A}{\\tl_set:Nn\\l_tmpb_tl{A}\\tl_if_eq:NNTF\\l_tmpa_tl\\l_tmpb_tl{T}{F}}\\tl_if_eq:NNTF\\l_tmpa_tl\\l_tmpb_tl{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\catcode`\\!=11\\tl_set:Nn\\l_tmpa_tl{!}\\catcode`\\!=12\\tl_set:Nn\\l_tmpb_tl{!}\\tl_if_eq:NNTF\\l_tmpa_tl\\l_tmpb_tl{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\def\\a{Q}\\let\\b\\a\\tl_if_eq:NNTF\\a\\b{T}{F}\\ExplSyntaxOff"
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
    "\\ExplSyntaxOn\\begin{document}\\tl_if_eq:nnTF{A}{A}{T}{F}\\end{document}",
    "\\ExplSyntaxOn\\begin{document}\\textbf{\\tl_if_eq:NNTF\\l_tmpa_tl\\l_tmpb_tl{T}{F}}\\end{document}",
    "\\def\\x{Hi}\\ExplSyntaxOn\\tl_if_eq:nnTF{}{A}{T}{F}\\ExplSyntaxOff\\begin{document}\\x\\end{document}"])
    safe_digestion(source)^]
