import timeline: lambda.chart.timeline
import dom

let spec={id:"held-filters",width:620,scales:[{kind:"numeric",unit:"year"}],
    value:{filters:{},query:""},search:{controls:true,fields:["title"]},groups:[{id:"g",title:"History"}],
    events:[{id:"a",at:10,title:"Alpha",group:"g"},{id:"b",at:20,title:"Beta"}]};
let model=timeline.model(spec);
view <held_filter_demo> state requests:0 {
    <html <body style:"font-family:Arial;margin:16px;",
        <output id:"requests",string(requests)> apply(model)>>
}
on timeline_request(value) { if (value.kind=="state_change") { requests=requests+1 } }
apply(<held_filter_demo>)
