import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let bodies=["\\prop_item:Nn\\l_missing_prop{x}","\\prop_if_in:NnTF\\l_missing_prop{x}{Y}{N}","\\prop_new:N\\l_p_prop\\edef\\x{\\l_p_prop}\\meaning\\x", "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{k}{A}\\edef\\x{\\l_p_prop}\\meaning\\x", "\\prop_new:N\\l_p_prop\\def\\a{A}\\prop_put:Nnn\\l_p_prop{k}{\\a}[\\prop_item:Nn\\l_p_prop{k}]", "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{k}{A}\\prop_put:Nnn\\l_p_prop{k}{B}\\prop_if_in:NnTF\\l_p_prop{k}{Y}{N}"];
fn check(body) any^ {
 let source="\\ExplSyntaxOn" ++ body ++ "\\ExplSyntaxOff";
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let n=parse(source,{type:"tex"})^;
 {body:body,issue:r.issue,unsupported:r.unsupported,text:if(r.issue==null)reconstruct.reconstruct(r)^.text==n.text else null,
  actual:if(r.issue==null)reconstruct.reconstruct(r)^.text else null,native:n.text,
  diagnostics:r.diagnostic_records==n.diagnostics,messages:r.messages==n.messages}
}
let results=[for(body in bodies)check(body)^];
let missing=profile.expand_with_kernel("\\ExplSyntaxOn\\prop_put:Nnn\\l_missing_prop{x}{y}",kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
let custom_kernel=kernel ++ "\n\\expandafter\\def\\csname c_empty_prop\\endcsname{USER}";
let preloaded=profile.expand_with_kernel("\\ExplSyntaxOn\\c_empty_prop\\ExplSyntaxOff",custom_kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
{cases:len(results),missing_guard:missing.issue=="tex-unsupported-primitive" and missing.unsupported=="__prop_split_wrong:Nw",preloaded:preloaded.issue==null and reconstruct.reconstruct(preloaded)^.text=="USER",failures:[for(r in results where r.issue!=null or not r.text or not r.diagnostics or not r.messages)r]}
