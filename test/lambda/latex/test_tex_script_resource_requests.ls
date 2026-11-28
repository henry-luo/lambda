import facade: lambda.doc.latex.tex_engine.facade

let empty={resources:{local:{},bundled:{}},resource_requests:true};
let first=facade.tex("\\input{child}",empty,100000)^;
let fallback=facade.tex("\\input{child}",
    {resources:{local:{child:{missing:true}},bundled:{}},resource_requests:true},100000)^;
let package=facade.tex("\\usepackage{sample}",empty,100000)^;
let kernel=facade.host_options(null, null, "\\relax");
let preload=facade.host_kernel_preload(kernel,false)^;
let captured=facade.host_capture(kernel,"local","child",null)^;
let ready=facade.host_capture(captured,"local","child.tex","World")^;
let result=facade.tex("\\input{child}",ready,100000,preload)^;
let changed=facade.host_capture(ready,"local","child.tex","Again")^;
let repeated=facade.tex("\\input{child}",changed,100000,preload)^;
// A presence check depends on the captured resource even when it reads no bytes.
let lookup=facade.host_options(ready, null, "\\IfFileExists{child.tex}{Y}{N}");
let resource_dependent=facade.host_kernel_preload(lookup,false)^ == null;
// A missing extension and its captured replacement retry the same input step,
// keeping the definition and text emitted before it.
let source="\\def\\greeting{Hello}\\greeting\\input{child}!";
let paused=facade.host_parse(source,kernel,"tex",preload,null)^;
let paused_again=facade.host_parse(source,captured,"tex",preload,paused.checkpoint)^;
let resumed=facade.host_parse(source,ready,"tex",preload,paused_again.checkpoint)^;
let fresh=facade.tex(source,ready,100000,preload)^;
{requests:[first.request,fallback.request,package.request],
    issues:[first.issue,fallback.issue,package.issue],text:result.root.text,issue:result.issue,
    repeated:repeated.root.text, resource_probe_requires_captures:resource_dependent,
    resumed_requests:[paused.request.name,paused_again.request.name],
    resumed_prefix:paused.checkpoint.state.expansions > 0 and len(paused.checkpoint.state.output) > 0,
    resumed_matches_fresh:resumed.root == fresh.root and resumed.issue == null}
