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
 "\\begin{document}A\\ifeof0 Y\\else N\\fi B\\end{document}",
 "\\begin{document}A\\ifeof15 Y\\else N\\fi B\\end{document}",
 "\\begin{document}A\\unless\\ifeof0 N\\else Y\\fi B\\end{document}",
 "\\begin{document}A\\ifeof0 Y\\fi B\\end{document}",
 "\\begin{document}A\\ifeof0\\iftrue Y\\else N\\fi\\else N\\fi B\\end{document}",
 "\\begin{document}A\\iffalse N\\else\\ifeof2 Y\\else N\\fi\\fi B\\end{document}",
 "\\count0=3\\begin{document}A\\ifeof\\count0 Y\\else N\\fi B\\end{document}",
 "\\def\\n{2 }\\begin{document}A\\ifeof\\n Y\\else N\\fi B\\end{document}",
 "\\begin{document}A\\ifeof\\numexpr1+2\\relax Y\\else N\\fi B\\end{document}",
 "\\let\\e=\\ifeof\\begin{document}A\\e0 Y\\else N\\fi B\\end{document}",
 "\\begin{document}A\\ifeof-1 Y\\else N\\fi B\\end{document}",
 "\\begin{document}A\\ifeof16 Y\\else N\\fi B\\end{document}",
 "\\begin{document}A\\ifeof999 Y\\else N\\fi B\\end{document}",
 "\\begin{document}$a\\ifeof0 x\\else y\\fi b$\\end{document}",
 "\\edef\\x{\\ifeof0 A\\else B\\fi}\\begin{document}\\x\\end{document}"
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
    "\\begin{document}A\\ifeof0 Y\\else N\\fi B\\end{document}",
    "\\begin{document}$a\\ifeof0 x\\else y\\fi b$\\end{document}",
    "\\def\\x{Hi}\\ifeof0 Y\\else N\\fi\\begin{document}\\x\\end{document}"])
    safe_digestion(source)^]
