import profile: lambda.doc.latex.tex_engine.profile
import expansion: lambda.doc.latex.tex_engine.expansion
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let base={echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
let legacy={*:base,script_expl3:true,constructor_argument_mode:false};
let constructor={*:legacy,constructor_argument_mode:true};
let l=profile.preload_expl3_kernel(kernel,legacy,100000)^;
let c=profile.preload_expl3_kernel(kernel,constructor,100000)^;
let source="\\ExplSyntaxOn\\seq_new:N\\l_s_seq\\textbf{\\seq_put_left:Nn\\l_s_seq{A}B}\\ExplSyntaxOff";
fn result(r) any^ => {text:reconstruct.reconstruct(r)^.text,messages:r.messages,diagnostics:r.diagnostic_records,issue:r.issue}
let ca=profile.expand_with_kernel(source,kernel,constructor,100000,l)^;
let cf=profile.expand_with_kernel(source,kernel,constructor,100000)^;
let la=profile.expand_with_kernel(source,kernel,legacy,100000,c)^;
let lf=profile.expand_with_kernel(source,kernel,legacy,100000)^;
let spoof={*:l,constructor_argument_mode:true};
let sf=profile.expand_with_kernel(source,kernel,constructor,100000,spoof)^;
let public_result=parse(source,{type:"tex"})^;
// the constructor text and diagnostic were captured independently from the native engine.
{constructor:result(ca)^,constructor_matches_fresh:result(ca)^==result(cf)^,
 legacy_matches_fresh:result(la)^==result(lf)^,spoof_matches_fresh:result(sf)^==result(cf)^,
 public_matches:result(ca)^.text == public_result.text and result(ca)^.diagnostics == public_result.diagnostics};
let marked=expansion.resume(profile.preload_kernel(kernel,base,100000)^,"\\def\\CacheProbe{C}","<caller-format>",100000)^;
let reused=profile.expand_with_kernel("\\CacheProbe",kernel,base,100000,marked)^;
{same_options_reuse:reconstruct.reconstruct(reused)^.text};
let first=kernel ++ "\n\\def\\CacheProbe{A}";
let second=kernel ++ "\n\\def\\CacheProbe{B}";
let saved=profile.preload_kernel(first,base,100000)^;
let different=profile.expand_with_kernel("\\CacheProbe",second,base,100000,saved)^;
{different_kernel:reconstruct.reconstruct(different)^.text};
let one={*:base,base_path:"fixture/main.tex",source_name:"fixture/main.tex",resources:{local:{'child.tex':{data:"A"}},bundled:{}}};
let two={*:one,resources:{local:{'child.tex':{data:"B"}},bundled:{}}};
let cached=profile.preload_kernel(kernel,one,100000)^;
let changed=profile.expand_with_kernel("\\input{child.tex}",kernel,two,100000,cached)^;
{different_resource:reconstruct.reconstruct(changed)^.text,issue:changed.issue};
let resource_kernel=kernel ++ "\n\\input{child.tex}";
let a={*:base,resources:{local:{},bundled:{'child.tex':{data:"\\def\\CacheProbe{A}"}}}};
let b={*:base,resources:{local:{},bundled:{'child.tex':{data:"\\def\\CacheProbe{B}"}}}};
let prior=profile.preload_kernel(resource_kernel,a,100000)^;
let rebound={*:prior,resources:b.resources};
let actual=profile.expand_with_kernel("\\CacheProbe",resource_kernel,b,100000,rebound)^;
{rebound_resource:reconstruct.reconstruct(actual)^.text,issue:actual.issue};
let missing={*:base,resources:{local:{},bundled:{'child.tex':{missing:true}}}};
let missing_saved=profile.preload_kernel(resource_kernel,missing,100000)^;
let missing_rebound={*:missing_saved,resources:b.resources};
let corrected=profile.expand_with_kernel("\\CacheProbe",resource_kernel,b,100000,missing_rebound)^;
{previously_missing:reconstruct.reconstruct(corrected)^.text,diagnostics:corrected.diagnostic_records}
;
// a recorded missing resource also affects preload state when requests are enabled.
let missing_requested={*:missing,resource_requests:true};
let requested_saved=profile.preload_kernel(resource_kernel,missing_requested,100000)^;
let requested_new={*:b,resource_requests:true};
let requested_rebound={*:requested_saved,resources:requested_new.resources};
let requested_result=profile.expand_with_kernel("\\CacheProbe",resource_kernel,requested_new,100000,requested_rebound)^;
{previously_missing_requested:reconstruct.reconstruct(requested_result)^.text,diagnostics:requested_result.diagnostic_records}
