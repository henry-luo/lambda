import timeline: lambda.chart.timeline
let model=timeline.model({id:"controls",view:"axis",width:700,height:320,
    scales:[{kind:"numeric",unit:"year"}],window:{from:0,to:100},
    toolbar:{position:"bottom",sticky:true,buttons:["quick_jump","zoom_in","reset","layout","theme"],
        labels:{zoom_in:"Magnify"},icons:{zoom_in:<span class:"zoom-icon","+">}},
    events:[{id:"a",at:0,title:"First"},{id:"b",at:50,title:"Second"},{id:"c",at:100,title:"Third"}]});
<html <body style:"margin:20px;",apply(model)>>
