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
 "\\newenvironment{x}{A}{B}\\newenvironment{x}{C}{D}X",
 "\\newenvironment{foo-bar}{A}{B}\\newenvironment{foo-bar}{C}{D}X",
 "\\newenvironment*{x}{A}{B}\\newenvironment*{x}{C}{D}X",
 "\\newenvironment{x}[1]{A#1}{B}\\newenvironment{x}[1]{C#1}{D}X",
 "\\newenvironment{x}{A}{B}\\newenvironment{x} {C}{D}X",
 "\\newenvironment{x}{A}{B}\\newenvironment{x}\n{C}\n{D}X",
 "\\newenvironment{x}{A}{B}\\newenvironment{x}{C} {D}X",
 "\\newenvironment{x}{A}{B}\\newenvironment{x}{C}{D}\\newenvironment{x}{E}{F}X",
 "\\newenvironment{x}{A}{B}\\newenvironment{y}{C}{D}X",
 "\\newenvironment{x}{A}{B}\\renewenvironment{x}{C}{D}X"
]) check(body)^]

import reconstruct: lambda.doc.latex.tex_engine.reconstruct
fn check_recovery(source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{constructor_argument_mode:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let b=reconstruct.reconstruct(r)^; let n=parse(source,{type:"tex"})^;
 {issue:r.issue,reconstruct_issue:b.issue,text_match:b.text==n.text,diagnostics_match:r.diagnostic_records==n.diagnostics}
}
[for (source in [
 "\\newenvironment{x}{A}{B}\\newenvironment{x}{C}{D}\\begin{document}\\begin{x}Z\\end{x}\\end{document}",
 "\\newenvironment{x}[1]{A#1}{B}\\newenvironment{x}[1]{C#1}{D}\\begin{document}\\begin{x}{Q}Z\\end{x}\\end{document}"
]) check_recovery(source)^]
