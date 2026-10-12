import timeline: lambda.chart.timeline
import dom

let initial={id:"source-ui",width:620,source:{revision:"r1"},interaction:{focus_on_load:true,multiselect:true},
    default_value:{active_id:"b",selected_ids:["a","b"]},
    events:[{id:"a",title:"Alpha"},{id:"b",title:"Beta"},{id:"c",title:"Gamma"}]};
let model=timeline.model(initial);
view <source_demo> state activations:0,selections:0 {
    <html <body style:"font-family:Arial;margin:16px;",<main
        <output id:"activations",string(activations)><output id:"selections",string(selections)>
        <button id:"empty",type:"button","Replace with empty source"> apply(model) >>>
}
on keydown(evt) {
    if (evt.key=="x") {
        let host=dom.closest(evt.target,"[data-timeline-host]");
        if (host!=null) { dom.dispatch(host,{type:"timeline_action",detail:{type:"source_update",expected_revision:"r1",
            spec:{*:initial,source:{revision:"r2"},events:[initial.events[0],initial.events[2]]}}}) }
    }
    'pass'
}
on click(evt) {
    if (dom.get_attribute(evt.target,"id")=="empty") {
        let host=dom.query_selector(dom.closest(evt.target,"main"),"[data-timeline-host]");
        dom.dispatch(host,{type:"timeline_action",detail:{type:"source_update",spec:{*:initial,default_value:{},source:{revision:"r3"},events:[]}}})
    }
    'pass'
}
on timeline_activate(value) { activations=activations+1 }
on timeline_select(value) { selections=selections+1 }
apply(<source_demo>)
