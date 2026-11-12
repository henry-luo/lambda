import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let root="test/lambda/latex/fixtures/local_packages/";
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let source=input(root ++ "script_openin.tex","text")^;
let resources={local:{'script_provenance.sty':{data:input(root ++ "script_provenance.sty","text")^},
    'script_provenance.tex':{data:input(root ++ "script_provenance.tex","text")^},
    'script_stream_empty.tex':{data:""}},bundled:{}};
let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:resources},100000)^;
let rebuilt=reconstruct.reconstruct(r)^;
let native=input(root ++ "script_openin.tex","tex")^;
{issue:r.issue,reconstruct_issue:rebuilt.issue,matches:rebuilt.text==native.text,diagnostics_match:r.diagnostic_records==native.diagnostics}
let ast=digest.digest(r)^;
if (ast.ast != null) ast.ast == (input(root ++ "script_openin.tex","latex")^) else ast.issue=="latex-digest-unsupported"
