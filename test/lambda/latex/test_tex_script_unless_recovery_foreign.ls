import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let root = "test/lambda/latex/fixtures/local_packages/";
let main = root ++ "script_unless_main.tex";
let child = "script_unless_child.tex";
let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
let resources = {local:{'script_unless_child.tex':{data:input(root ++ child,"text")^}}, bundled:{}};
let script = profile.expand_with_kernel(input(main,"text")^, kernel,
    {source_name:main, echo_definitions:true, adapters:[], resources:resources}, 100000)^;
let native = input(main,"tex")^;
let rebuilt = reconstruct.reconstruct(script)^;
{issue:script.issue, text_match:rebuilt.text == native.text,
 diagnostics_match:script.diagnostic_records == native.diagnostics,
 file_recorded:len(script.diagnostic_records) == 1 and script.diagnostic_records[0].file == child}
