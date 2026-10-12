import timeline: lambda.chart.timeline
import numbers: lambda.chart.numbers
import dom
let initial={id:"editing",view:"axis",width:600,height:400,padding:0,toolbar:false,
    scales:[{kind:"numeric",unit:"year"}],window:{from:0,to:100},source:{revision:"r0"},
    edit:{enabled:true,snap_step:5,confirm:true},events:[{id:"a",from:20,to:40,title:"Editable range"},{id:"b",at:80,title:"Fixed point",editable:false}]}
let history=timeline.model(initial)
view <editing_demo> state host:null,source:initial,request:null,commits:0,errors:0 {
    <html <body style:"margin:0;font-family:Arial;",<main <button id:"arm",type:"button","Connect application">
        <output id:"proposal",if (request==null) "none" else numbers.concise_number(request.proposed.from) ++ "/" ++ numbers.concise_number(request.proposed.to)>
        <output id:"commits",string(commits)> <output id:"errors",string(errors)>
        apply(history) >>>
}
on click(evt) {
    if (dom.get_attribute(evt.target,"id")=="arm") { host=dom.query_selector(dom.closest(evt.target,"main"),"[data-timeline-host]") }
    'pass'
}
on timeline_edit_request(value) { request=value }
on timeline_edit_commit(value) {
    source=timeline.apply_edit(source,value);
    commits=commits+1;
    source={*:source,source:{revision:"r" ++ string(commits)}};
    dom.dispatch(host,{type:"timeline_action",detail:{type:"source_update",expected_revision:value.source_revision,spec:source}})
}
on timeline_error(value) { errors=errors+1 }
apply(<editing_demo>)
