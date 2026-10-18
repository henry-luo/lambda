import timeline: lambda.chart.timeline
import dom
let initial={id:"resource-life",source:{url:"first.json",revision:"r1"},media_options:{timeout:100,concurrency:1,retry:1}}
let history=timeline.model(initial)
view <resource_lifetime_demo> state request:null,first:null,starts:0,cancels:0,deadlines:0,completions:0 {
    <html <body <main
        <button id:"revise",type:"button","Replace source">
        <button id:"stale",type:"button","Deliver retired result">
        <button id:"deliver",type:"button","Deliver current result">
        <button id:"expire",type:"button","Start a source that times out">
        <output id:"starts",string(starts)> <output id:"cancels",string(cancels)>
        <output id:"deadlines",string(deadlines)> <output id:"completions",string(completions)>
        apply(history)
    >>>
}
on timeline_request(value) {
    if (value.kind=="resource") {
        request=value;
        if (first==null) { first=value }
        starts=starts+1
    } else if (value.kind=="cancel_resource") { cancels=cancels+1 }
}
on timeline_resource(value) {
    // classify error outcomes without propagating them through comparisons (S7.9.1).
    if (not (value.error is null)) { deadlines=deadlines+1 } else { completions=completions+1 }
}
on click(evt) {
    let key=dom.get_attribute(evt.target,"id");
    let host=dom.query_selector(dom.closest(evt.target,"main"),"[data-timeline-host]");
    if (key=="revise" or key=="expire") {
        dom.dispatch(host,{type:"timeline_action",detail:{type:"source_update",spec:{*:initial,
            source:{url:if (key=="revise") "second.json" else "timeout.json",revision:if (key=="revise") "r2" else "r3"}}}})
    } else if (key=="deliver" or key=="stale") {
        let reply=if (key=="stale") first else request;
        dom.dispatch(host,{type:"timeline_action",detail:{type:"resource_result",id:reply.id,token:reply.token,revision:reply.revision,
            result:[{id:"loaded",title:if (key=="stale") "Retired history" else "Current history"}]}})
    }
    'pass'
}
apply(<resource_lifetime_demo>)
