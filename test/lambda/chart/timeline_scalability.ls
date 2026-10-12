import timeline: lambda.chart.timeline
import prepared: lambda.chart.timeline.prepared
import geometry: lambda.chart.timeline.layout
fn unmeasured(item, context) error => error("unclustered card was measured")

let raw={id:"indexed",view:"axis",width:"auto",height:400,scales:[{kind:"numeric",unit:"year"}],window:{from:500,to:505},
    events:[for (position in 0 to 1023) {id:string(position),at:position,title:"Event " ++ string(position),tags:[position%2]}]}
let source=prepared.source(raw)
let initial=timeline.initial_state(raw)
let first=geometry.normalized(source.normalized,{width:800,height:400},initial)
let moved={*:initial,window:{from:800,to:805}}
let reused=prepared.source(raw,moved,source)
let second=geometry.normalized(reused.normalized,{width:800,height:400},moved)
let filtered=prepared.source(raw,{*:moved,filters:{tags:[0]}},reused)
let narrowed=geometry.normalized(filtered.normalized,{width:800,height:400},{*:moved,filters:{tags:[0]}})
let selected=prepared.source(raw,{*:moved,selected_ids:["800"]},reused)
let hidden=prepared.source({*:raw,groups:[{id:"hidden",collapsed:true}],events:[{id:"a",at:501,group:"hidden"},{id:"b",at:502}]})
let dense={*:raw,window:{from:-1,to:1},cluster:{enabled:true},events:[for (position in 0 to 63)
    {id:string(position),at:0,title:"Dense",content:unmeasured}]}
let dense_plan=timeline.layout(dense,{width:800,height:400})
let cached=prepared.geometry(source.normalized,{width:800,height:400},initial)
let same=prepared.geometry(source.normalized,{width:800,height:400},{*:initial,selected_ids:["500"]},cached);
[
 ["window uses interval index",first.work.indexed and first.work.examined<=32],
 ["inclusive boundaries and canonical order",(first.events |> ~.id)==["500","501","502","503","504","505"]],
 ["window movement retains normalization",reused.normalizations==1 and reused.visibility_builds==1],
 ["second window remains bounded",second.work.examined<=32 and second.work.measured==6],
 ["selection retains visibility",selected==reused],
 ["filters rebuild visibility only",filtered.normalizations==1 and filtered.visibility_builds==2],
 ["filtered index has canonical matches",(narrowed.events |> ~.id)==["800","802","804"]],
 ["collapsed groups excluded from index",len(hidden.normalized._timeline.visible_index.events)==1],
 ["clustering precedes rich card measurement",not (dense_plan is error) and dense_plan.work.measured==1],
 ["cluster retains source membership",dense_plan.clusters[0].count==64 and len(dense_plan.spec.events)==64],
 ["selection reuses measured geometry",same==cached],
 ["viewport invalidates measured geometry",prepared.geometry(source.normalized,{width:900,height:400},initial,cached).plan.width==900]
]
