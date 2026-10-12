import timeline: lambda.chart.timeline
let history=timeline.model({id:"native-end",view:"story",spacing:"sequence",width:600,height:220,
    slideshow:{duration:100,media_end:true,media_timeout:30000},toolbar:{buttons:["play"]},
    events:[{id:"a",title:"Native end notification",media:[{id:"audio",kind:"audio",
        src:"../../media/test_audio.m4a",start:2.95,muted:true}]},{id:"b",title:"Completed"}]})
view <native_end_demo> state ended:0,errors:0,timeouts:0 {
    <html <body style:"margin:0;font-family:Arial;",
        <output id:"ended",string(ended)> <output id:"errors",string(errors)> <output id:"timeouts",string(timeouts)>
        apply(history)
    >>
}
on timeline_media(value) {
    if (value.status=="ended") { ended=ended+1 } else if (value.status=="error") { errors=errors+1 }
}
on timeline_media_timeout(value) { timeouts=timeouts+1 }
apply(<native_end_demo>)
