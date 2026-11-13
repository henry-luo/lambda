import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn check(body) any^ {
    let source = "\\begin{document}" ++ body ++ "\\end{document}";
    let expanded = profile.expand_with_kernel(source, kernel, {constructor_argument_mode: true, echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}}, 100000)^;
    let result = digest.digest(expanded)^;
    let native = parse(source, {type: "latex"}) ^ { null };
    {body: body, issue: result.issue, unsupported: result.unsupported,
        matches: if (result.ast == null or native == null) null else result.ast == native,
        actual: if (result.ast == null or native == null or result.ast == native) null else result.ast,
        native: if (result.ast == null or native == null or result.ast == native) null else native}
}
[for (body in [
 "\\renewcommand{\\x}{A}X", "\\renewcommand*{\\foo}{A}X",
 "\\renewcommand{\\foo}[1]{A#1}X", "\\renewcommand{\\foo}[1][v]{A#1}X",
 "\\renewcommand{\\foo} {A}X", "\\renewcommand{\\foo}\n{A}X",
 "\\newcommand{\\x}{A}\\newcommand{\\x}{B}X",
 "\\newcommand*{\\x}{A}\\newcommand*{\\x}{B}X",
 "\\newcommand{\\x}[1]{A#1}\\newcommand{\\x}[1]{B#1}X",
 "\\newcommand{\\x}{A}\\newcommand{\\x} {B}X",
 "\\newcommand{\\x}{A}\\renewcommand{\\x}{B}X",
 "\\providecommand{\\x}{A}\\providecommand{\\x}{B}X",
 "\\renewcommand{\\x}{A}\\renewcommand{\\y}{B}X",
 "\\escapechar=-1\\renewcommand{\\x}{A}X",
 "\\escapechar=43\\newcommand{\\x}{A}\\newcommand{\\x}{B}X"
]) check(body)^]

import reconstruct: lambda.doc.latex.tex_engine.reconstruct
fn check_recovery(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{constructor_argument_mode:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let b=reconstruct.reconstruct(r)^; let n=parse(source,{type:"tex"})^;
 {issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,diagnostics_match:r.diagnostic_records==n.diagnostics}
}
[for (source in [
 "\\newcommand{\\x}{A}\\newcommand{\\x}{B}\\begin{document}\\x\\end{document}",
 "\\renewcommand{\\x}{A}\\begin{document}\\x\\end{document}",
 "\\newcommand{\\x}[1]{A#1}\\newcommand{\\x}[1]{B#1}\\begin{document}\\x{Q}\\end{document}"
]) check_recovery(source)^]
