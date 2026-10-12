import timeline: lambda.chart.timeline
import motion: lambda.chart.timeline.motion
import vector: lambda.chart.timeline.svg

let raw={id:"motion",view:"axis",width:800,height:400,animate:{duration:200},scales:[{kind:"numeric",unit:"year"}],
    window:{from:0,to:100},events:[{id:"a",at:20,title:"Alpha"},{id:"b",at:60,title:"Beta"}]}
fn target(raw) {
    let plan=timeline.layout(raw,{width:800,height:400});
    {plan:plan,svg:vector.render(plan,null,{unsupported:"omit",cards:false})}
}
let a=target(raw)
let next={*:raw,window:{from:0,to:200},events:[{id:"b",at:60,title:"Beta"},{id:"c",at:140,title:"Gamma"}]}
let b=target(next)
let spec=b.plan.spec
let middle=motion.sample(a,b,100,spec)
let end=motion.sample(a,b,200,spec)
let entry=motion.advance(null,a,0,a.plan.spec)
let entered=motion.advance(entry,a,100,a.plan.spec)
let settled=motion.advance(entered,a,200,a.plan.spec)
let started=motion.advance(settled,b,300,spec)
let partial=motion.advance(started,b,400,spec)
let c=target({*:next,window:{from:0,to:300},events:[{id:"b",at:80,title:"Beta"},{id:"c",at:200,title:"Gamma"}]})
let interrupted=motion.advance(partial,c,400,c.plan.spec)
let half=(middle.plan.events |: ~.id=="b")[0]
let old=(a.plan.events |: ~.id=="b")[0]
let final=(b.plan.events |: ~.id=="b")[0]
let frozen=timeline.render_frame(next,{time_ms:100,previous:a},{width:800,height:400});
[
 ["enter starts transparent",entry.displayed.plan.events[0].opacity==0 and entry.running],
 ["enter samples explicit time",entered.displayed.plan.events[0].opacity==0.5],
 ["frame scheduling retires",not settled.running and settled.displayed==a],
 ["keyed update interpolates position",abs(half.point[0]-(old.point[0]+final.point[0])/2.0)<0.000001],
 ["keyed update interpolates cards",abs(half.bounds.left-(old.bounds.left+final.bounds.left)/2.0)<0.000001],
 ["enter samples opacity",(middle.plan.events |: ~.id=="c")[0].opacity==0.5],
 ["exit is passive and retained",(middle.plan.events |: ~.id=="a")[0].exiting==true and (middle.plan.events |: ~.id=="a")[0].opacity==0.5],
 ["exit retires at completion",(end.plan.events |> ~.id)==["b","c"] and end==b],
 ["window and picking use sampled bounds",middle.plan.window=={from:0m,to:150m} and middle.plan.time_map.domain==middle.plan.window],
 ["interruption retains visible positions",interrupted.displayed.plan.events[0].point==partial.displayed.plan.events[0].point],
 ["interruption retains partial enter opacity",interrupted.displayed.plan.events[1].opacity==partial.displayed.plan.events[1].opacity],
 ["reduced motion settles immediately",motion.sample(a,b,0,{*:spec,motion:"reduced"})==b],
 ["disabled animation settles immediately",motion.sample(a,b,0,{*:spec,animate:motion.configure(false)})==b],
 ["delay retains source geometry",motion.sample(a,b,50,{*:spec,animate:motion.configure({duration:200,delay:100})}).plan.events[0].point==old.point],
 ["custom easing uses shared progress",motion.sample(a,b,100,{*:spec,animate:motion.configure({duration:200,easing:(t)=>t*t})}).plan.events[1].opacity==0.25],
 ["negative duration rejected",timeline.validate({*:raw,animate:{duration:-1}}) is error],
 ["unknown options rejected",timeline.validate({*:raw,animate:{unknown:true}}) is error],
 ["invalid easing rejected",timeline.validate({*:raw,animate:{easing:"unknown"}}) is error],
 ["negative sample time rejected",motion.sample(a,b,-1,spec) is error],
 ["explicit frame produces presentation",frozen is element]
]
