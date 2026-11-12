import utf8: lambda.io.utf8
import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
fn matches(source) any^ {
    let result = profile.expand_with_kernel(source, kernel,
        {echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let rebuilt = reconstruct.reconstruct(result)^;
    let native = parse(source, {type: "tex"})^;
    {issue: result.issue, reconstruct_issue: rebuilt.issue, matches: rebuilt.text == native.text,
        diagnostics_match: result.diagnostic_records == native.diagnostics, messages_empty: len(result.messages) == 0}
}
let sources = [
 "\\begin{document}A\\the\\nullfont B\\end{document}",
 "\\begin{document}A\\the\\font B\\end{document}",
 "\\begin{document}A\\the\\textfont0 B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\begin{document}A\\the\\f B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\f\\begin{document}A\\the\\font B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\textfont1=\\f\\begin{document}A\\the\\textfont1 B\\end{document}",
 "\\edef\\x{\\the\\nullfont}\\begin{document}\\meaning\\x\\end{document}",
 "\\edef\\x{\\the\\textfont0}\\begin{document}\\meaning\\x\\end{document}",
 "\\font\\f=cmr10 at 13pt\\edef\\x{\\the\\f}\\begin{document}\\meaning\\x\\end{document}",
 "\\font\\f=cmr10 at 13pt\\f\\edef\\x{\\the\\font}\\begin{document}\\meaning\\x\\end{document}",
 "\\font\\f=cmr10 at 13pt\\textfont1=\\f\\edef\\x{\\the\\textfont1}\\begin{document}\\meaning\\x\\end{document}",
 "\\font\\f=cmr10 at 13pt\\textfont1=\\f\\def\\f{ZZ}\\begin{document}A\\the\\textfont1 B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\font\\g=cmr10 at 13pt\\edef\\x{\\the\\g}\\begin{document}\\meaning\\x\\end{document}",
 "\\font\\f=cmr10 at 13pt\\font\\g=cmr10 at 13pt\\def\\f{ZZ}\\begin{document}A\\the\\g B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\let\\g=\\f\\edef\\x{\\the\\g}\\begin{document}\\meaning\\x\\end{document}",
 "\\font\\f=cmr10 at 13pt\\textfont1=\\f\\edef\\x{\\the\\textfont1}\\def\\f{ZZ}\\begin{document}A\\x B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\textfont1=\\f\\def\\f{ZZ}\\edef\\x{\\the\\textfont1}\\begin{document}A\\meaning\\x!\\x B\\end{document}"
];
[for (source in sources) matches(source)^]

fn safe_digestion(source) any^ {
    let result = profile.expand_with_kernel(source, kernel,
        {echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let parsed = digest.digest(result)^;
    if (parsed.ast != null) parsed.ast == (parse(source, {type: "latex"})^)
    else parsed.issue == "latex-digest-unsupported"
}
[for (source in [
    "\\begin{document}A\\the\\nullfont B\\end{document}",
    "\\begin{document}$x\\the\\nullfont y$\\end{document}",
    "\\begin{document}A\\textbf{B\\the\\nullfont C}D\\end{document}",
    "\\edef\\x{\\the\\nullfont}\\begin{document}A\\x B\\end{document}"])
    safe_digestion(source)^]
