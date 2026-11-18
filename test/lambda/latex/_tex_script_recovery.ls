import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct

let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
let options = {echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}};

pub fn check(source) any^ {
    let result = profile.expand_with_kernel(source, kernel, options, 100000)^;
    let native = parse(source, {type: "tex"})^;
    {issue: result.issue, text: reconstruct.reconstruct(result)^.text == native.text,
        messages: result.messages == native.messages, diagnostics: result.diagnostic_records == native.diagnostics}
}
