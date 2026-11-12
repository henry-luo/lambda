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
 "\\font\\f=cmr10 at 13pt\\letterspacefont\\g\\f 100\\begin{document}A\\the\\fontdimen6\\g B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\letterspacefont\\g\\f 100\\begin{document}A\\ifx\\f\\g T\\else F\\fi B\\end{document}",
 "\\letterspacefont\\g\\nullfont 0\\begin{document}A\\the\\hyphenchar\\g B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\f\\letterspacefont\\g\\font -100\\begin{document}A\\the\\fontdimen6\\g B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\textfont1=\\f\\letterspacefont\\g\\textfont1 100\\begin{document}A\\ifx\\f\\g T\\else F\\fi B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\letterspacefont\\g\\f \\numexpr50+50\\relax\\begin{document}A\\the\\fontdimen6\\g B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\count0=100\\letterspacefont\\g\\f \\count0\\begin{document}A\\ifx\\f\\g T\\else F\\fi B\\end{document}",
 "\\font\\f=cmr10 at 13pt{\\letterspacefont\\g\\f 100}\\begin{document}A\\ifdefined\\g T\\else F\\fi B\\end{document}",
 "\\font\\f=cmr10 at 13pt{\\global\\letterspacefont\\g\\f 100}\\begin{document}A\\ifx\\f\\g T\\else F\\fi B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\globaldefs=1{\\letterspacefont\\g\\f 100}\\begin{document}A\\ifx\\f\\g T\\else F\\fi B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\globaldefs=-1{\\global\\letterspacefont\\g\\f 100}\\begin{document}A\\ifdefined\\g T\\else F\\fi B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\begin{document}A\\afterassignment X\\letterspacefont\\g\\f 100 B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\begin{document}A\\afterassignment X\\global\\letterspacefont\\g\\f 100 B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\let\\ls=\\letterspacefont\\ls\\g\\f 100\\begin{document}A\\ifx\\f\\g T\\else F\\fi B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\letterspacefont\\g\\f 100\\hyphenchar\\g=63\\begin{document}A\\the\\hyphenchar\\f B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\letterspacefont\\g\\f 100\\edef\\x{\\the\\g}\\begin{document}A\\meaning\\x B\\end{document}",
 "\\font\\f=cmr10 at 13pt\\letterspacefont\\g\\f 100\\def\\f{ZZ}\\begin{document}A\\the\\g B\\end{document}",
 "\\letterspacefont\\g\\nullfont 0\\let\\x=\\g\\begin{document}A\\ifx\\x\\g T\\else F\\fi B\\end{document}"
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
    "\\begin{document}A\\letterspacefont\\g\\nullfont 0 B\\end{document}",
    "\\def\\x{Hi}\\letterspacefont\\g\\nullfont 0\\begin{document}A\\x B\\end{document}"])
    safe_digestion(source)^]
