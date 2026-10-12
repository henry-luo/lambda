import timeline: lambda.chart.timeline
let spec={id:"story-timing",view:"story",spacing:"sequence",width:600,height:320,
    slideshow:{duration:100,delay:50,transition:"fade",transition_duration:40,progress:true,overall_progress:true},
    toolbar:{buttons:["play","pause","resume"]},events:[{id:"a",title:"First"},{id:"b",title:"Second"}]}
let history=timeline.model(spec);
<html <body style:"margin:0;font-family:Arial;",apply(history)>>
