import profile: lambda.doc.latex.tex_engine.profile
let root="test/lambda/latex/fixtures/local_packages/";
let main=root ++ "script_env_diagnostic_main.tex";
let child="script_env_diagnostic_child.tex";
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let source=input(main,"text")^;
let resources={local:{'script_env_diagnostic_child.tex':{data:input(root ++ child,"text")^}},bundled:{}};
let result=profile.expand_with_kernel(source,kernel,{source_name:main,echo_definitions:true,adapters:[],resources:resources},100000)^;
let native=input(main,"tex")^;
{issue:result.issue,diagnostics_match:result.diagnostic_records==native.diagnostics,
 file_recorded:len(result.diagnostic_records)==1 and result.diagnostic_records[0].file==child}
