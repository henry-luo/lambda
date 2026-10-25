import slide: lambda.slide
import html: lambda.slide.html

let kinds = ['blinds-in','checkerboard-in','dissolve-in','line-color','transparency','grow','motion'];
<html <head <style html.css ++ "html,body{background:#fff;margin:0}body{display:flex;flex-wrap:wrap;width:640px}.slide-player{height:100px}">>;
    <body *[for (kind in kinds) slide.player(<presentation width:160.0,height:100.0,
        <slide background:"#ffffff",
            <shape id:"box",x:16.0,y:10.0,width:128.0,height:80.0,fill:"#ff0000",stroke:"#000000",stroke_width:8.0>
            <cue <effect target:"box",kind:kind,duration:1000.0,easing:'linear',
                *:if (kind == 'line-color') {color:"#ffffff"} else if (kind == 'transparency') {opacity:0.5}
                    else if (kind == 'grow') {scale:2.0} else if (kind == 'motion') {path:[[0.0,0.0,80.0,0.0]]}
                    else if (kind in ['blinds-in','checkerboard-in']) {from:'left'} else {}>>>>,
        {instance:"fx-" ++ string(kind),width:160.0,height:100.0,controls:false})^];
        slide.player(<presentation width:160.0,height:100.0,<slide background:"#ffffff",advance_on_click:false,
            <shape id:"box",x:16.0,y:10.0,width:80.0,height:80.0,fill:"#ff0000">
            <group id:"hotspot",x:120.0,y:25.0,width:32.0,height:50.0,
                <shape id:"button",width:32.0,height:50.0,fill:"#00ff00">>
            <cue trigger:"hotspot",<effect target:"box",kind:'fade-in',duration:1000.0>>
            <cue <effect target:"button",kind:'spin',duration:1000.0>>>
            <slide background:"#0000ff",transition:'fade',transition_duration:1000.0>>,
            {instance:"fx-trigger",width:160.0,height:100.0,controls:false})^>>
