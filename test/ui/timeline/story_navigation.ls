import timeline: lambda.chart.timeline
let spec={id:"story-navigation",view:"story",spacing:"sequence",width:600,height:220,
    title:"A shared history",story:{title_slide:{id:"intro",title:"Welcome",content:<p "An introduction">},order:["c","a","b"]},
    toolbar:{buttons:["first","previous","next","last","quick_jump"]},navigator:{visible:true},
    events:[{id:"a",title:"First"},{id:"b",title:"Second"},{id:"c",title:"Third"}]}
let history=timeline.model(spec);
<html <body style:"margin:0;font-family:Arial;",apply(history)>>
