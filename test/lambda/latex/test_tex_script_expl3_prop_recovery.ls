import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import compare: ._tex_expl3_compare
let kernel=compare.kernel;
let bodies=["\\prop_item:Nn\\l_missing_prop{x}","\\prop_if_in:NnTF\\l_missing_prop{x}{Y}{N}","\\prop_new:N\\l_p_prop\\edef\\x{\\l_p_prop}\\meaning\\x", "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{k}{A}\\edef\\x{\\l_p_prop}\\meaning\\x", "\\prop_new:N\\l_p_prop\\def\\a{A}\\prop_put:Nnn\\l_p_prop{k}{\\a}[\\prop_item:Nn\\l_p_prop{k}]", "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{k}{A}\\prop_put:Nnn\\l_p_prop{k}{B}\\prop_if_in:NnTF\\l_p_prop{k}{Y}{N}"];
fn check(body, fresh = false) any^ {
 let source="\\ExplSyntaxOn" ++ body ++ "\\ExplSyntaxOff";
 let r=compare.expand(source,fresh)^;
 let n=parse(source,{type:"tex"})^;
 {body:body,issue:r.issue,unsupported:r.unsupported,text:if(r.issue==null)reconstruct.reconstruct(r)^.text==n.text else null,
  actual:if(r.issue==null)reconstruct.reconstruct(r)^.text else null,native:n.text,
  diagnostics:r.diagnostic_records==n.diagnostics,messages:r.messages==n.messages}
}
let results=[for(i,body in bodies)check(body,i == 0)^];
let missing_source="\\ExplSyntaxOn\\prop_put:Nnn\\l_missing_prop{x}{y}";
let missing=compare.expand(missing_source)^;
let missing_public=parse(missing_source,{type:"tex"}) ^ {null};
let custom_kernel=kernel ++ "\n\\expandafter\\def\\csname c_empty_prop\\endcsname{USER}";
let preloaded=profile.expand_with_kernel("\\ExplSyntaxOn\\c_empty_prop\\ExplSyntaxOff",custom_kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
{cases:len(results),missing_guard:missing.issue=="tex-unsupported-primitive" and missing.unsupported=="__msg_interrupt:NnnnN" and missing_public==null,preloaded:preloaded.issue==null and reconstruct.reconstruct(preloaded)^.text=="USER",failures:[for(r in results where r.issue!=null or not r.text or not r.diagnostics or not r.messages)r]}
