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
 "\\begin{document}A\\the\\interactionmode B\\end{document}",
 "\\interactionmode=3\\begin{document}A\\the\\interactionmode B\\end{document}",
 "\\batchmode\\nonstopmode\\scrollmode\\errorstopmode\\begin{document}A\\the\\interactionmode B\\end{document}",
 "\\begin{document}A\\the\\pagegoal B\\end{document}",
 "\\begin{document}A\\the\\pagetotal B\\end{document}",
 "\\begin{document}A\\the\\pagestretch B\\end{document}",
 "\\begin{document}A\\the\\pagefilstretch B\\end{document}",
 "\\begin{document}A\\the\\pagefillstretch B\\end{document}",
 "\\begin{document}A\\the\\pagefilllstretch B\\end{document}",
 "\\begin{document}A\\the\\pageshrink B\\end{document}",
 "\\begin{document}A\\the\\pagedepth B\\end{document}",
 "\\pagegoal=1pt\\begin{document}A\\the\\pagegoal B\\end{document}",
 "\\pagetotal=\\dimexpr2pt+3pt\\relax\\begin{document}A\\the\\pagetotal B\\end{document}",
 "\\count0=\\interactionmode\\begin{document}A\\the\\count0 B\\end{document}",
 "\\dimen0=\\pagegoal\\begin{document}A\\the\\dimen0 B\\end{document}",
 "\\begin{document}A\\ifnum\\interactionmode=1 T\\else F\\fi B\\end{document}",
 "\\begin{document}A\\ifdim\\pagegoal=0pt T\\else F\\fi B\\end{document}",
 "\\begin{document}A\\afterassignment X\\interactionmode=3 B\\end{document}",
 "\\begin{document}A\\afterassignment X\\global\\pagegoal=1pt B\\end{document}",
 "\\let\\p=\\pagegoal\\p=2pt\\begin{document}A\\the\\p B\\end{document}",
 "\\let\\i=\\interactionmode\\i=2\\begin{document}A\\the\\i B\\end{document}",
 "\\begin{document}A\\the\\numexpr\\interactionmode+2\\relax B\\end{document}",
 "\\begin{document}A\\the\\dimexpr\\pagegoal+2pt\\relax B\\end{document}"
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
    "\\begin{document}A\\the\\pagegoal B\\end{document}",
    "\\begin{document}$x+\\the\\pagegoal$\\end{document}",
    "\\begin{document}A\\textbf{B\\the\\interactionmode C}D\\end{document}",
    "\\begin{document}A\\pagegoal=1pt B\\end{document}",
    "\\def\\x{Hi}\\pagegoal=1pt\\begin{document}A\\x B\\end{document}"])
    safe_digestion(source)^]
