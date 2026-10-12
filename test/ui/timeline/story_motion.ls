import timeline: lambda.chart.timeline
let history=timeline.model({id:"story-motion",view:"story",spacing:"sequence",width:600,height:240,
    slideshow:{duration:100,transition:"fade",transition_duration:40},toolbar:{buttons:["play","pause","resume"]},
    events:[{id:"a",title:"Outgoing film",content:<a href:"#old","Old link">,
        media:[{kind:"video",src:"../../media/test_video.mp4",poster:"data:image/svg+xml,%3Csvg%20xmlns='http://www.w3.org/2000/svg'%20width='20'%20height='20'%3E%3Crect%20width='20'%20height='20'%20fill='red'/%3E%3C/svg%3E"}]},
        {id:"b",title:"Incoming history"},{id:"c",title:"Last history"}]},null,{media:false});
<html <body style:"margin:0;font-family:Arial;",apply(history)>>
