import timeline: lambda.chart.timeline
let spec={id:"viewport",view:"axis",width:800,height:400,scales:[{kind:"numeric",unit:"year"}],window:{from:40,to:50},
    events:[for (index in 0 to 100) {id:string(index),at:index,title:"Event " ++ string(index)}]}
let plan=timeline.layout(spec,{width:800,height:400})
let initial=timeline.initial_state(spec)
let activated=timeline.update(initial,{type:"activate",id:"90"},{spec:spec}).state
let next=timeline.layout(spec,{width:800,height:400},activated)
let rendered=timeline.render(spec,{width:800,height:400},initial)
let chronology=(content(rendered) |: ~.class=="lambda-timeline-chronology")[0];
[
 ["measure visible events",len(plan.events)==11 and len(plan.visible_events)==11],
 ["full source retained",plan.total_events==101 and len(plan.spec.events)==101],
 ["offscreen count",plan.offscreen_count==90],
 ["semantic chronology bounded",len(content(chronology))==11],
 ["offscreen summary",len(content(rendered) |: ~.class=="lambda-timeline-window-summary")==1],
 ["activation follows event",activated.window.from<=90 and activated.window.to>=90],
 ["follow preserves span",activated.window.to-activated.window.from==10],
 ["new window rendered",contains(next.events |> ~.id,"90")],
 ["auto scroll opt out",timeline.update(initial,{type:"activate",id:"90"},{spec:{*:spec,interaction:{auto_scroll:false}}}).state.window==initial.window],
 ["point at boundary retained",contains(plan.events |> ~.id,"40") and contains(plan.events |> ~.id,"50")],
 ["overlapping range retained",len(timeline.layout({*:spec,events:[{id:"range",from:0,to:100}]},{width:800,height:400}).events)==1]
]
