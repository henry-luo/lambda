import timeline: lambda.chart.timeline

let model=timeline.model({id:"filter-controls",width:620,scales:[{kind:"numeric",unit:"year"}],
    groups:[{id:"g1",title:"First group"},{id:"g2",title:"Second group"}],search:{controls:true},
    events:[{id:"a",at:10,title:"Alpha",group:"g1",tags:["old"]},
        {id:"b",at:20,title:"Beta",group:"g2",tags:["new"]},{id:"c",at:30,title:"Gamma",group:"g1",tags:["new"]}]});
<html <body style:"font-family:Arial;margin:16px;",apply(model)>>
