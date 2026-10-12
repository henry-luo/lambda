import timeline: lambda.chart.timeline
let model=timeline.model({id:"cluster",view:"axis",width:800,height:400,scales:[{kind:"numeric",unit:"year"}],window:{from:0,to:100},
    cluster:{enabled:true,distance:30,min_count:3},toolbar:false,
    events:[{id:"a",at:20,title:"Alpha"},{id:"b",at:21,title:"Beta"},{id:"c",at:22,title:"Gamma"},{id:"d",at:80,title:"Delta"}]});
<html <body apply(model)>>
