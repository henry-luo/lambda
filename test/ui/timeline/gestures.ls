import timeline: lambda.chart.timeline
let model=timeline.model({id:"gestures",view:"axis",width:800,height:400,padding:0,
    scales:[{kind:"numeric",unit:"year"}],window:{from:0,to:100},toolbar:false,cards:{visibility:"active"},
    interaction:{pan:true,zoom:true,click_to_use:true,wheel_modifier:"ctrl"},
    events:[{id:"a",at:20,title:"First"},{id:"b",at:80,title:"Last"}]});
<html <body style:"margin:0;",apply(model)>>
