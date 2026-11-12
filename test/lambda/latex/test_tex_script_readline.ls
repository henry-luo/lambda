import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let root="test/lambda/latex/fixtures/local_packages/";
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let resources={local:{'script_stream_data.dat':{data:input(root ++ "script_stream_data.dat","text")^},
    'script_stream_letter.dat':{data:input(root ++ "script_stream_letter.dat","text")^},
    'script_stream_empty.tex':{data:""}},bundled:{}};
fn check(name) any^ {
    let source=input(root ++ name ++ ".tex","text")^;
    let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:resources},100000)^;
    let rebuilt=reconstruct.reconstruct(r)^;
    let native=input(root ++ name ++ ".tex","tex")^;
    let ast=digest.digest(r)^;
    {issue:r.issue,reconstruct_issue:rebuilt.issue,matches:rebuilt.text==native.text,
        diagnostics_match:r.diagnostic_records==native.diagnostics,
        ast_safe:if(ast.ast != null) ast.ast==(input(root ++ name ++ ".tex","latex")^) else ast.issue=="latex-digest-unsupported"}
}
[for(name in ["script_readline","script_readline_closed","script_readline_scope","script_readline_endline","script_readline_endline_utf8","script_readline_endline_default"]) check(name)^]
