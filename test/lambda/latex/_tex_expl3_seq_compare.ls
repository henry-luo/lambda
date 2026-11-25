import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct

pub let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
pub let baseline = parse("", {type:"tex"})^.messages;
let options = {script_expl3:true, echo_definitions:true, adapters:[], resources:{local:{}, bundled:{}}};
// Each comparison resumes a fresh document from the same immutable kernel.
let preload = profile.preload_expl3_kernel(kernel, options, 100000)^;
pub fn expand(source) any^ => profile.expand_with_kernel(source, kernel, options, 100000, preload)
pub fn check(source) any^ {
    let result = expand(source)^;
    let native = parse(source, {type:"tex"})^;
    let built = reconstruct.reconstruct(result)^;
    {issue:result.issue, reconstruct_issue:built.issue, text_match:built.text == native.text,
        messages_match:result.messages == slice(native.messages, len(baseline), len(native.messages))^,
        diagnostics_match:result.diagnostic_records == native.diagnostics}
}
