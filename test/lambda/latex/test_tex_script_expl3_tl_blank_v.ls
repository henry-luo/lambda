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
 "\\ExplSyntaxOn\\tl_if_blank:VTF\\l_tmpa_tl{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{~}\\tl_if_blank:VTF\\l_tmpa_tl{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{~~~}\\tl_if_blank:VTF\\l_tmpa_tl{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{A}\\tl_if_blank:VTF\\l_tmpa_tl{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{~A~}\\tl_if_blank:VTF\\l_tmpa_tl{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{{}}\\tl_if_blank:VTF\\l_tmpa_tl{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_blank:VT\\l_tmpa_tl{T}\\tl_set:Nn\\l_tmpa_tl{A}\\tl_if_blank:VF\\l_tmpa_tl{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_blank:VF\\l_tmpa_tl{F}\\tl_set:Nn\\l_tmpa_tl{A}\\tl_if_blank:VT\\l_tmpa_tl{T}\\ExplSyntaxOff",
 "\\def\\x{}\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{\\x}\\tl_if_blank:VTF\\l_tmpa_tl{T}{F}\\ExplSyntaxOff",
 "\\def\\x{}\\ExplSyntaxOn\\tl_set:Nx\\l_tmpa_tl{\\x}\\tl_if_blank:VTF\\l_tmpa_tl{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn{\\tl_set:Nn\\l_tmpa_tl{A}\\tl_if_blank:VTF\\l_tmpa_tl{T}{F}}\\tl_if_blank:VTF\\l_tmpa_tl{T}{F}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_if_blank:VTF\\l_tmpa_tl{T}{\\seq_pop_left:NN\\l_missing_seq\\l_tmpb_tl}\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\edef\\x{\\tl_if_blank:VTF\\l_tmpa_tl{T}{F}}\\x\\ExplSyntaxOff",
 "\\ExplSyntaxOn\\tl_set:Nn\\l_tmpa_tl{A}\\edef\\x{\\tl_if_blank:VTF\\l_tmpa_tl{T}{F}}\\x\\ExplSyntaxOff",
 "\\def\\foo{Q}\\ExplSyntaxOn\\edef\\x{\\tl_if_blank:VTF\\l_tmpa_tl{\\noexpand\\foo}{F}}\\def\\foo{Z}\\x\\ExplSyntaxOff",
 "\\def\\foo{}\\ExplSyntaxOn\\tl_if_blank:VTF\\foo{T}{F}\\ExplSyntaxOff",
 "\\long\\def\\foo{}\\ExplSyntaxOn\\tl_if_blank:VTF\\foo{T}{F}\\ExplSyntaxOff",
 "\\protected\\def\\foo{}\\ExplSyntaxOn\\tl_if_blank:VTF\\foo{T}{F}\\ExplSyntaxOff",
 "\\def\\foo{~}\\ExplSyntaxOn\\tl_if_blank:VTF\\foo{T}{F}\\ExplSyntaxOff"
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
    "\\ExplSyntaxOn\\begin{document}\\tl_if_blank:VTF\\l_tmpa_tl{T}{F}\\end{document}",
    "\\def\\x{Hi}\\ExplSyntaxOn\\tl_if_blank:VTF\\l_tmpa_tl{T}{F}\\ExplSyntaxOff\\begin{document}\\x\\end{document}"])
    safe_digestion(source)^]
