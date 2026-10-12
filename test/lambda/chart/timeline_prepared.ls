import timeline: lambda.chart.timeline
import prepared: lambda.chart.timeline.prepared
import resource: lambda.chart.timeline.resource

fn label(value) => (item,context)=>value
let raw={id:"prepared",order:"chronological",events:[{id:"a",at:"2001",title:label("First")},{id:"b",at:"2000",title:"Earlier"}]}
let first=prepared.source(raw)
let updated=prepared.source({*:raw,events:[{*:raw.events[0],at:"1999"},raw.events[1]]},null,first)
let callback=prepared.source({*:raw,events:[{*:raw.events[0],title:label("Changed")},raw.events[1]]},null,first)
let remote={id:"remote",source:{url:"history.json",revision:"r1"},events:[{id:"authored",title:"Authored"}]}
let unloaded=prepared.source(remote)
let entry={status:"ready",revision:"r1",descriptor:resource.descriptors(unloaded.normalized)[0],result:[{id:"loaded",title:"Loaded"}]}
let received={resources:{source:entry}}
let loaded=prepared.source(remote,received,unloaded)
let refreshing={resources:{source:{*:entry,status:"pending",result:null,cached:true,cached_result:entry.result}}}
let retained=prepared.source(remote,refreshing,loaded)
let next=prepared.source(remote,{resources:{source:{*:entry,result:[{id:"new",title:"Replacement"}]}}},loaded)
let invalid=prepared.source(remote,{resources:{source:{*:entry,result:42}}},loaded)
let empty=prepared.source(remote,{resources:{source:{*:entry,result:[]}}},loaded);
[
 ["initial preparation normalizes ordering",(first.normalized.events |> ~.id)==["b","a"]],
 ["selection and query retain source normalization",prepared.source(raw,{selected_ids:["a"],query:"First"},first).normalizations==first.normalizations],
 ["media results do not change source inputs",prepared.source(raw,{resources:{image:{status:"ready",result:"pixels"}}},first)==first],
 ["changed dates invalidate preparation",(updated.normalized.events |> ~.id)==["a","b"]],
 ["changed callback captures invalidate preparation",callback.normalized.events[1].title({}, {})=="Changed"],
 ["model retains initial preparation",timeline.model(raw).prepared.normalized==first.normalized],
 ["controlled value changes invalidate preparation",prepared.source({*:raw,value:{active_id:"a"}},null,first).normalized.value.active_id=="a"],
 ["source arrival replaces preparation",(loaded.normalized.events |> ~.id)==["authored","loaded"]],
 ["source refresh retains cached data",retained.normalized==loaded.normalized],
 ["source completion replaces old loaded rows",(next.normalized.events |> ~.id)==["authored","new"]],
 ["changed revision invalidates loaded data",len(prepared.source({*:remote,source:{*:remote.source,revision:"r2"}},received,loaded).normalized.events)==1],
 ["changed URL invalidates loaded data",len(prepared.source({*:remote,source:{*:remote.source,url:"other.json"}},received,loaded).normalized.events)==1],
 ["invalid loaded data remains an error",invalid.normalized is error and invalid.fallback.events[0].id=="authored"],
 ["empty loaded data clears old rows",(empty.normalized.events |> ~.id)==["authored"]]
]
