import timeline: lambda.chart.timeline
import schema: lambda.chart.timeline.schema
import groups: lambda.chart.timeline.groups
import layout: lambda.chart.timeline.layout

let spec={id:"groups",view:"axis",width:900,height:600,scales:[{kind:"numeric",unit:"year"}],window:{from:0,to:100},cards:{stack:false},
    groups:[{id:"later",title:"Later",order:2,height:120,subgroups:[{id:"two",order:2},{id:"one",order:1}]},
        {id:"first",title:"First",order:1,groups:[{id:"child",title:"Child"}]}],
    events:[{id:"a",at:20,group:"later",subgroup:"one",title:"One"},
        {id:"b",at:20,group:"later",subgroup:"two",title:"Two"},
        {id:"c",at:40,group:"child",title:"Child"},{id:"d",at:60,group:"first",title:"First"}]}
let normalized=timeline.validate(spec)
let plan=timeline.layout(spec,{width:900,height:600})
let initial=timeline.initial_state(spec)
let collapsed=timeline.update(initial,{type:"collapse",group:"first"},{spec:spec,revision:1}).state
let closed=timeline.layout(spec,{width:900,height:600},collapsed)
let a=(plan.events |: ~.id=="a")[0]
let b=(plan.events |: ~.id=="b")[0]
let constrained=timeline.layout({*:spec,groups:[{id:"later",height:120,min_height:150,subgroups:[{id:"one"},{id:"two"}]},{id:"first",groups:[{id:"child"}]}]},
    {width:900,height:600})
let render=timeline.render(spec,{width:900,height:600},collapsed);
[
 ["hierarchy sibling order",(groups.ordered(normalized.groups) |> ~.id)==["first","child","later"]],
 ["subgroup order",(plan.lanes |: ~.group=="later" |> ~.subgroup)==["one","two"]],
 ["separate subgroup lanes",a.point!=b.point and a.fraction==b.fraction],
 ["explicit group height",(plan.groups |: ~.id=="later")[0].to-(plan.groups |: ~.id=="later")[0].from==120],
 ["minimum height",(constrained.groups |: ~.id=="later")[0].to-(constrained.groups |: ~.id=="later")[0].from==150],
 ["collapse hides owned and descendant events",(closed.events |> ~.id)==["a","b"]],
 ["collapse keeps header",contains(closed.groups |> ~.id,"first") and not contains(closed.groups |> ~.id,"child")],
 ["selection survives collapse",timeline.update({*:initial,selected_ids:["c"]},{type:"collapse",group:"first"},{spec:spec}).state.selected_ids==["c"]],
 ["hidden subgroup",len(timeline.layout({*:spec,groups:[{id:"later",subgroups:[{id:"one",visible:false},{id:"two"}]},{id:"first",groups:[{id:"child"}]}]},
    {width:900,height:600}).events)==3],
 ["unknown subgroup",schema.validate({events:[{id:"a",group:"g",subgroup:"missing"}],groups:[{id:"g"}]}) is error],
 ["duplicate subgroup IDs",schema.validate({groups:[{id:"g",subgroups:[{id:"s"},{id:'s'}]}]}) is error],
 ["height constraints diagnosed",schema.validate({groups:[{id:"g",min_height:100,max_height:50}]}) is error],
 ["rendered group controls",len(content(render) |: ~.class=="lambda-timeline-groups")==1]
]
