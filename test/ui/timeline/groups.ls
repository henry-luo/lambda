import timeline: lambda.chart.timeline
let model=timeline.model({id:"history",width:800,groups:[{id:"parent",title:"History",groups:[{id:"child",title:"Discoveries"}]}],
    events:[{id:"a",group:"child",title:"First"},{id:"b",title:"Independent"}]});
<html <body apply(model)>>
