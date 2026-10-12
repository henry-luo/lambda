import timeline: lambda.chart.timeline
import layout: lambda.chart.timeline.layout
import playback: lambda.chart.playback
let spec={id:"science",view:"axis",width:800,height:400,window:{from:"1900",to:"2000"},
    events:[{id:"a",at:"1910",title:"Alpha"},{id:"b",at:"1955",title:"Beta"},{id:"c",from:"1940",to:"1970",title:"Period"}],
    eras:[{id:"era",from:"1900",to:"1940",title:"Early"}]}
let plan=timeline.layout(spec,{width:800,height:400})
let alpha=(plan.events |: ~.id=="a")[0]
let beta=(plan.events |: ~.id=="b")[0]
let period=(plan.events |: ~.id=="c")[0]
let image=timeline.render_svg(spec,{width:800,height:400})
let rich={*:spec,events:[{id:"a",at:"1910",content:<p "Rich content">}]}
let clustered={*:spec,events:[{id:"a",at:"1950",title:"One"},{id:"b",at:"1950",title:"Two"},{id:"c",at:"1950",title:"Three"}]}
let stacked=timeline.layout(clustered,{width:800,height:400})
let old={timeline:{keyframes:[{at:0,spec:{}},{at:1000,spec:{}}]}};
[
 ["time proportionality",abs((beta.point[0]-alpha.point[0])/plan.box.width-0.45)<0.0001],
 ["range endpoints",period.end[0]>period.point[0]],
 ["visible ticks",len(plan.ticks)>1],
 ["SVG root",name(image)=='svg' and image.width==800 and image.height==400],
 ["rich policy required",timeline.render_svg(rich,{width:800,height:400}) is error],
 ["rich text export",name(timeline.render_svg(rich,{width:800,height:400},null,{unsupported:"text"}))=='svg'],
 ["picking",layout.pick(plan,plan.events[0].point).id=="a"],
 ["window clips events",len(timeline.layout({*:spec,window:{from:"1920",to:"1930"}},{width:800,height:400}).events)==0],
 ["stacked labels",stacked.events[0].card!=stacked.events[2].card and stacked.events[0].point==stacked.events[2].point],
 ["window constraints",layout.constrain({from:0,to:1},{min_span:5})=={from:-2,to:3}],
 ["pan bounds",layout.constrain({from:-5,to:5},{min:0,max:100})=={from:0,to:10}],
 ["invalid viewport",timeline.layout(spec,{width:0,height:400}) is error],
 ["playback configure alias",timeline.configure(old)==playback.configure(old)],
 ["playback inherit alias",timeline.inherit({}, {})==playback.inherit({}, {})],
 ["playback target alias",timeline.target(old)==playback.target(old)],
 ["playback position alias",timeline.position(timeline.configure(old),100)==playback.position(playback.configure(old),100)],
 ["playback segment alias",timeline.segment(old,100)==playback.segment(old,100)],
 ["playback initial alias",timeline.initial(old)==playback.initial(old)],
 ["playback command alias",timeline.command(timeline.initial(old),{command:"play"},0,timeline.configure(old))==
    playback.command(playback.initial(old),{command:"play"},0,playback.configure(old))]
]
