import timeline: lambda.chart.timeline
import dom
let spec={id:"media-lifetime",view:"story",spacing:"sequence",source:{revision:"r1"},width:600,height:220,
    slideshow:{duration:100,media_end:true,media_timeout:200},toolbar:{buttons:["play","pause","resume","stop"]},
    media_options:{providers:{test:{render:(record,context) => <p "Application player">}}},
    events:[{id:"a",title:"First",media:[{id:"film",kind:"video",provider:"test",src:"film.mp4"}]},{id:"b",title:"Second"}]}
let history=timeline.model(spec)
view <media_lifetime_demo> state request:null,first:null,starts:0,pauses:0,timeouts:0 {
    <html <body style:"margin:0;font-family:Arial;",<main
        <button id:"finish",type:"button","Finish current player">
        <button id:"stale",type:"button","Finish retired player">
        <output id:"starts",string(starts)> <output id:"pauses",string(pauses)> <output id:"timeouts",string(timeouts)>
        apply(history)
    >>>
}
on timeline_request(value) {
    if (value.kind=="media_command") {
        if (value.command=="play") { request=value; starts=starts+1; if (first==null) { first=value } }
        else if (value.command=="pause") { pauses=pauses+1 }
    }
}
on timeline_media_timeout(value) { timeouts=timeouts+1 }
on click(evt) {
    let key=dom.get_attribute(evt.target,"id");
    if (key=="finish" or key=="stale") {
        let reply=if (key=="stale") first else request;
        let host=dom.query_selector(dom.closest(evt.target,"main"),"[data-timeline-host]");
        dom.dispatch(host,{type:"timeline_action",detail:{type:"media_event",status:"ended",event_id:reply.event_id,
            id:reply.id,token:reply.token,revision:reply.revision}})
    }
    'pass'
}
apply(<media_lifetime_demo>)
