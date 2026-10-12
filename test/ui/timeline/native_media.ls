import timeline: lambda.chart.timeline
let history=timeline.model({id:"native-player",view:"story",spacing:"sequence",width:600,height:220,
    slideshow:{duration:100,media_end:true,media_timeout:30000},toolbar:{buttons:["play","pause","resume","stop"]},
    events:[{id:"a",title:"Native audio",media:[{id:"clip",kind:"audio",src:"../../media/test_audio.m4a",end:0,muted:true},
            {id:"missing",kind:"audio",src:"../../media/timeline_missing.m4a"}]},
        {id:"b",title:"After native completion"}]})
view <native_media_demo> state ended:0,failures:0,timeouts:0 {
    <html <body style:"margin:0;font-family:Arial;",<main
        <output id:"ended",string(ended)> <output id:"failures",string(failures)> <output id:"timeouts",string(timeouts)>
        apply(history)
    >>>
}
on timeline_media(value) {
    if (value.status=="ended") { ended=ended+1 } else if (value.status=="error") { failures=failures+1 }
}
on timeline_media_timeout(value) { timeouts=timeouts+1 }
apply(<native_media_demo>)
