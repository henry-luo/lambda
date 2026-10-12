import timeline: lambda.chart.timeline
let spec={id:"cluster",view:"axis",width:800,height:400,scales:[{kind:"numeric",unit:"year"}],window:{from:0,to:100},
    cluster:{enabled:true,distance:30,min_count:3,title:"{count} discoveries"},
    events:[{id:"a",at:20,title:"Alpha"},{id:"b",at:21,title:"Beta"},{id:"c",at:22,title:"Gamma"},{id:"d",at:80,title:"Delta"}]}
let plan=timeline.layout(spec,{width:800,height:400})
let group=plan.clusters[0]
let initial=timeline.initial_state(spec)
let expanded=timeline.update(initial,{type:"cluster_expand",id:group.id,ids:group.ids},{spec:spec}).state
let expanded_plan=timeline.layout(spec,{width:800,height:400},expanded)
let separated=timeline.layout({*:spec,cluster:{enabled:true,by:"color"},events:[{id:"a",at:20,color:"red"},{id:"b",at:21,color:"blue"},{id:"c",at:22,color:"red"}]},
    {width:800,height:400})
let same={*:spec,cluster:false,events:[{id:"a",at:20,title:"Equal",placement:"end"},{id:"b",at:20,title:"Equal",priority:10,placement:"end"},{id:"c",at:20,title:"Equal",placement:"end"}]}
let priority=timeline.layout(same,{width:800,height:400})
let solo=timeline.layout({*:same,events:[same.events[1]]},{width:800,height:400});
[
 ["distance aggregation",len(plan.events)==2 and len(plan.clusters)==1],
 ["canonical records retained",len(plan.spec.events)==4],
 ["cluster membership",group.ids==["a","b","c"] and group.count==3],
 ["cluster title",group.event.title=="3 discoveries"],
 ["representative chronology",abs(group.fraction-0.21)<0.000001],
 ["expanded members",len(expanded_plan.events)==4 and len(expanded_plan.clusters)==0],
 ["stable cluster key",timeline.layout(spec,{width:800,height:400}).clusters[0].id==group.id],
 ["group criteria",len(separated.events)==3 and len(separated.clusters)==0],
 ["expansion bounded",timeline.update(initial,{type:"cluster_expand",id:group.id,ids:group.ids},{spec:{*:spec,cluster:{enabled:true,max_expansion:2}}}) is error],
 ["removed member rejected",timeline.update(initial,{type:"cluster_expand",id:group.id,ids:["missing"]},{spec:spec}) is error],
 ["fractional count diagnosed",timeline.validate({*:spec,cluster:{min_count:2.5}}) is error],
 ["priority owns placement",priority.events[1].bounds==solo.events[0].bounds],
 ["priority preserves order",(priority.events |> ~.id)==["a","b","c"]]
]
