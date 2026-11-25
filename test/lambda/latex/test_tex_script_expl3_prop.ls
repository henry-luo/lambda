import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let edges=["\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{\\foo}{X}\\escapechar=-1[\\prop_item:Nn\\l_p_prop{\\foo}]",
 "\\prop_new:N\\l_p_prop\\escapechar=-1\\prop_put:Nnn\\l_p_prop{\\foo}{X}\\escapechar=92[\\prop_item:Nn\\l_p_prop{\\foo}]",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{A}{X}\\catcode`A=12[\\prop_item:Nn\\l_p_prop{A}]",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{ a~b }{X}[\\prop_item:Nn\\l_p_prop{a~b}]",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{\\foo}{X}[\\prop_item:Nn\\l_p_prop{\\foo~}]",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{a}{X}\\prop_put:Nnn\\l_p_prop{b}{Y}\\prop_put:Nnn\\l_p_prop{a}{Z}[\\meaning\\l_p_prop]",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{a}{X}\\prop_if_in:NnT\\l_p_prop{a}{Y}\\prop_if_in:NnF\\l_p_prop{b}{Y}",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{a}{X}\\prop_if_in:NnT\\l_p_prop{b}{N}\\prop_if_in:NnF\\l_p_prop{a}{N}"];
let advanced=["[\\meaning\\prop_new:N/\\meaning\\prop_put:Nnn/\\meaning\\prop_gput:Nnn]",
 "[\\meaning\\prop_item:Nn/\\meaning\\prop_if_in:NnTF/\\meaning\\prop_if_in:NnT/\\meaning\\prop_if_in:NnF]",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{k}{X}\\prop_if_in:NnT\\l_p_prop{k}{Y}\\prop_if_in:NnF\\l_p_prop{k}{N}",
 "\\prop_new:N\\l_p_prop\\prop_if_in:NnT\\l_p_prop{k}{N}\\prop_if_in:NnF\\l_p_prop{k}{Y}",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{k}{X}\\let\\p\\prop_item:Nn[\\p\\l_p_prop{k}]",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{k}{X}\\let\\q\\prop_if_in:NnTF\\q\\l_p_prop{k}{Y}{N}",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{\\foo}{X}[\\prop_item:Nn\\l_p_prop{\\foo}]",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{a~b}{X}[\\prop_item:Nn\\l_p_prop{a~b}]",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{abc}{X}\\edef\\x{\\prop_item:Nn\\l_p_prop{abc}}[\\meaning\\x]",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{a}{X}\\prop_put:Nnn\\l_p_prop{b}{Y}\\prop_put:Nnn\\l_p_prop{a}{Z}[\\meaning\\l_p_prop]",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{k}{{A}B}[\\prop_item:Nn\\l_p_prop{k}]",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{k}{\\relax A}[\\prop_item:Nn\\l_p_prop{k}]",
 "\\prop_new:N\\l_p_prop\\prop_put:Nnn\\l_p_prop{k}{X}\\ifx\\l_p_prop\\l_p_prop Y\\else N\\fi"];
let bodies=[*edges,*advanced];
fn check(body) any^ {
 let source="\\ExplSyntaxOn" ++ body ++ "\\ExplSyntaxOff";
 let r=profile.expand_with_kernel(source,kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let n=parse(source,{type:"tex"})^;
 {body:body,issue:r.issue,unsupported:r.unsupported,text:if(r.issue==null)reconstruct.reconstruct(r)^.text==n.text else null,
  actual:if(r.issue==null)reconstruct.reconstruct(r)^.text else null,native:n.text,
  diagnostics:r.diagnostic_records==n.diagnostics,messages:r.messages==n.messages}
}
let results=[for(body in bodies)check(body)^];
let duplicate=profile.expand_with_kernel("\\ExplSyntaxOn\\prop_new:N\\l_p_prop\\prop_new:N\\l_p_prop X",kernel,{script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
{cases:len(results),duplicate_guard:duplicate.issue=="tex-unsupported-primitive" and duplicate.unsupported=="msg_error:nnee",failures:[for(r in results where r.issue!=null or not r.text or not r.diagnostics or not r.messages)r]}
