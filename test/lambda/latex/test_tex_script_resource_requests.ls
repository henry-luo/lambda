import facade: lambda.doc.latex.tex_engine.facade

let empty={resources:{local:{},bundled:{}},resource_requests:true};
let first=facade.tex("\\input{child}",empty,100000)^;
let fallback=facade.tex("\\input{child}",
    {resources:{local:{child:{missing:true}},bundled:{}},resource_requests:true},100000)^;
let package=facade.tex("\\usepackage{sample}",empty,100000)^;
let captured=facade.host_capture(facade.host_options(null),"local","child",null)^;
let ready=facade.host_capture(captured,"local","child.tex","World")^;
let result=facade.tex("\\input{child}",ready,100000)^;
{requests:[first.request,fallback.request,package.request],
    issues:[first.issue,fallback.issue,package.issue],text:result.root.text,issue:result.issue}
