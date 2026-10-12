import timeline: lambda.chart.timeline
import dom
let history=timeline.model({id:"reentrant",events:[{id:"a",title:"First"},{id:"b",title:"Second"},{id:"c",title:"Third"}]})
view <reentrant_demo> state host:null,notices:0 {
    <html <body <main <button id:"go",type:"button","Activate second and redirect to third">
        <output id:"notices",string(notices)> apply(history) >>>
}
on click(evt) {
    if (dom.get_attribute(evt.target,"id")=="go") {
        host=dom.query_selector(dom.closest(evt.target,"main"),"[data-timeline-host]");
        dom.dispatch(host,{type:"timeline_action",detail:{type:"activate",id:"b"}})
    }
    'pass'
}
on timeline_activate(value) {
    notices=notices+1;
    if (value.value=="b") { dom.dispatch(host,{type:"timeline_action",detail:{type:"activate",id:"c"}}) }
}
apply(<reentrant_demo>)
