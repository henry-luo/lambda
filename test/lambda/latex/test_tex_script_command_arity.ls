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
 "\\newcommand{\\x}[10]{A}X", "\\newcommand{\\x}[-1]{A}X",
 "\\newcommand{\\x}[2]{#1/#2}X", "\\renewcommand{\\x}[10]{A}X",
 "\\newcommand{\\x}[11]{A}X", "\\newcommand{\\x}[100]{A}X",
 "\\newcommand{\\x}[+0011]{A}X", "\\newcommand{\\x}[-123]{A}X",
 "\\newcommand{\\x}[11foo]{A}X", "\\newcommand{\\x}[foo]{A}X",
 "\\newcommand{\\x}[10][Z]{A}X", "\\newcommand{\\x} [10]{A}X",
 "\\newcommand{\\x}\n[10]{A}X", "\\newcommand{\\x}[2147483647]{A}X",
 "\\newcommand{\\x}[2147483648]{A}X",
 "\\newcommand{\\x}[-2147483648]{A}X",
 "\\renewcommand{\\x}[100]{A}\\renewcommand{\\y}[-3]{B}X"
]) check(body)^]

let long_source="\\renewcommand{\\aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa}{A}";
let long_result=profile.expand_with_kernel(long_source,kernel,{adapters:[],resources:{local:{},bundled:{}}},100000)^;
let long_native=parse(long_source,{type:"tex"})^;
{long_name_diagnostics_match:long_result.diagnostic_records==long_native.diagnostics}
