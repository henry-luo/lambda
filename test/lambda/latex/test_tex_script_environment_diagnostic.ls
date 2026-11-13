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
 "\\renewenvironment{x}{A}{B}X", "\\renewenvironment{foo-bar}{A}{B}X",
 "\\renewenvironment*{foo}{A}{B}X", "\\renewenvironment{x}[1]{A#1}{B}X",
 "\\renewenvironment{x}[1][v]{A#1}{B}X", "\\renewenvironment{x} {A}{B}X",
 "\\renewenvironment{x}\n{A}\n{B}X", "\\renewenvironment{x}{A} {B}X",
 "\\renewenvironment{x}{A}{B}\\renewenvironment{y}{C}{D}X",
 "\\newenvironment{x}{A}{B}\\renewenvironment{x}{C}{D}X",
 "\\renewenvironment{x}{A}{B}\\renewenvironment{x}{C}{D}X",
 "\\renewenvironment{x}%comment\n{A}{B}X"
]) check(body)^]

let dynamic_source="\\begin{document}\\def\\envname{x}\\renewenvironment{\\envname}{A}{B}X\\end{document}";
let dynamic=profile.expand_with_kernel(dynamic_source,kernel,{constructor_argument_mode:true,echo_definitions:true,
    adapters:[],resources:{local:{},bundled:{}}},100000)^;
let native_dynamic=parse(dynamic_source,{type:"tex"})^;
{dynamic_diagnostics_match:dynamic.diagnostic_records==native_dynamic.diagnostics}
