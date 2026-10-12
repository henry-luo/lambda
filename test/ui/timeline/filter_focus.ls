import timeline: lambda.chart.timeline
import dom

let model=timeline.model({id:"filter-focus",width:640,default_value:{active_id:"b",selected_ids:["b"]},
    interaction:{focus_on_load:true},search:{fields:["title"],mode:"filter"},
    groups:[{id:"g",title:"History"}],events:[{id:"a",title:"Alpha",group:"g"},
        {id:"b",title:"Beta",group:"g"},{id:"c",title:"Gamma"}]})
view <filter_demo> {
    <html <body style:"font-family:Arial;margin:16px;",apply(model)>>
}
on keydown(evt) {
    if (evt.key=="c" and dom.closest(evt.target,"input,textarea,select")==null) {
        let host=dom.closest(evt.target,"[data-timeline-host]");
        if (host!=null) { dom.dispatch(host,{type:"timeline_action",detail:{type:"collapse",group:"g"}}) }
    }
    'pass'
}
apply(<filter_demo>)
