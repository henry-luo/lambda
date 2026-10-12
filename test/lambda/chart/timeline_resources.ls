import t: lambda.chart.timeline
import resource: lambda.chart.timeline.resource
let raw={id:"live",source:{url:"history.json",format:"json",revision:"r1"},media_options:{concurrency:1,retry:1}}
let spec=t.validate(raw)
let current=t.initial_state(raw)
let context={spec:raw,revision:"r1",now:0}
let started=t.update(current,{type:"resource_request",id:"source"},context)
let token=started.requests[0].token
let reply={type:"resource_result",id:"source",token:token,revision:"r1",result:[{id:"a",at:"2020",title:"Loaded"}]}
let received=t.update(started.state,reply,{*:context,now:50})
let normalized=resource.normalize(raw,received.state)
let failed=t.update(started.state,{*:reply,result:null,error:error("offline")},{*:context,now:30})
let restarted=t.update(failed.state,{type:"resource_request",id:"source"},{*:context,now:40})
let changed={*:raw,source:{*:raw.source,url:"updated.json",revision:"r2"}}
let retired=t.update(started.state,{type:"resource_tick"},{spec:changed,revision:"r2",now:60})
let timed=t.update(started.state,{type:"resource_tick"},{*:context,now:30000})
let retry=t.update(timed.state,{type:"resource_request",id:"source"},{*:context,now:30001})
let exhausted=t.update(retry.state,{type:"resource_tick"},{*:context,now:60001})
let image_raw={source:{revision:"r1"},events:[{id:"a",media:[{src:"a.png"}]},{id:"b",media:[{src:"b.png"}]}]}
let images=t.validate(image_raw)
let image_state=t.initial_state(image_raw)
let descriptor=resource.descriptors(images,image_state)[0]
let loading_image=resource.begin(image_state,descriptor,"r1",0)
let next_active={*:loading_image.state,active_id:"b"}
let kept=resource.lifecycle(images,next_active,50)
let invalid=t.update(started.state,{*:reply,result:{events:[{id:"bad",group:"missing"}]}},{*:context,now:50})
let grouped_raw={id:"group-source",source:{url:"grouped.json",revision:"r1",mapping:{id:{field:"key"},group:{field:"lane"}}},groups:[{id:"g"}]}
let grouped_start=t.update(t.initial_state(grouped_raw),{type:"resource_request",id:"source"},{spec:grouped_raw,revision:"r1",now:0})
let grouped_result=t.update(grouped_start.state,{*:reply,token:grouped_start.requests[0].token,result:[{key:"mapped",lane:"g"}]},
    {spec:grouped_raw,revision:"r1",now:50})
let refreshing=resource.begin(received.state,resource.descriptors(spec)[0],"r1",75)
let refreshed=t.update(refreshing.state,{*:reply,token:refreshing.request.token,result:[{id:"a",title:"Refreshed"}]},{*:context,now:100})
let refresh_bad=t.update(refreshing.state,{*:reply,token:refreshing.request.token,result:42},{*:context,now:100});
[
 ["source descriptor",resource.descriptors(spec)[0].url=="history.json"],
 ["pending request",resource.pending(spec,current)[0].id=="source"],
 ["unique token",token!=null and started.state.resources.source.status=="pending"],
 ["bounded concurrency",len(resource.pending(spec,started.state))==0],
 ["recorded result",received.state.resources.source.status=="ready"],
 ["source materialization",normalized.events[0].title=="Loaded" and normalized.source.revision=="r1"],
 ["pure loaded presentation",t.render(raw,null,received.state) is element],
 ["ready not requested",len(resource.pending(spec,received.state,100))==0],
 ["unknown result ignored",t.update(current,reply,context).state==current],
 ["stale token ignored",t.update(started.state,{*:reply,token:"old"},context).state==started.state],
 ["stale revision ignored",t.update(started.state,reply,{*:context,revision:"r2"}).state==started.state],
 ["failure recorded",failed.state.resources.source.status=="error" and failed.state.resources.source.error is error],
 ["bounded retry",resource.pending(spec,failed.state)[0].id=="source" and restarted.requests[0].token!=token],
 ["late first request ignored",t.update(restarted.state,reply,context).state==restarted.state],
 ["unknown resource diagnostic",t.update(current,{type:"resource_request",id:"missing"},context) is error],
 ["request carries deadline",started.state.resources.source.deadline==30000 and started.requests[0].timeout==30000],
 ["revision retires pending token",retired.state.resources=={} and retired.requests[0].kind=="cancel_resource" and retired.requests[0].token==token],
 ["revision frees concurrency slot",resource.pending(t.validate(changed),retired.state,60)[0].url=="updated.json"],
 ["deadline records error",timed.state.resources.source.status=="error" and timed.notifications[0].detail.error is error],
 ["deadline cancels once",len(timed.requests)==1 and t.update(timed.state,{type:"resource_tick"},{*:context,now:30010}).requests==[]],
 ["late result cannot beat deadline",t.update(started.state,reply,{*:context,now:30000}).state.resources.source.status=="pending"],
 ["deadline retry is bounded",retry.requests[0].token!=token and len(resource.pending(spec,exhausted.state,70000))==0],
 ["ready resources do not keep clock",not resource.needs_clock(spec,received.state) and resource.needs_clock(spec,started.state)],
 ["priority change does not reload",kept.requests==[] and kept.state.resources[descriptor.id].token==loading_image.request.token],
 ["changed URL rejects recorded source",len(resource.normalize({*:raw,source:{*:raw.source,url:"other.json"}},received.state).events)==0],
 ["refresh interval",len(resource.pending(t.validate({*:raw,source:{*:raw.source,refresh:25}}),received.state,74))==0 and
    resource.pending(t.validate({*:raw,source:{*:raw.source,refresh:25}}),received.state,75)[0].id=="source"],
 ["failed refresh respects retry limit",len(resource.pending(t.validate({*:raw,source:{*:raw.source,refresh:25}}),exhausted.state,70000))==0],
 ["invalid source becomes resource error",invalid.state.resources.source.status=="error" and invalid.notifications[0].detail.error is error],
 ["source validates against authored groups",resource.normalize(grouped_raw,grouped_result.state).events[0].group=="g"],
 ["refresh retains last valid data",resource.normalize(raw,refreshing.state).events[0].title=="Loaded"],
 ["refresh replaces without duplication",len(resource.normalize(raw,refreshed.state).events)==1 and resource.normalize(raw,refreshed.state).events[0].title=="Refreshed"],
 ["bad refresh retains last valid data",refresh_bad.state.resources.source.status=="error" and resource.normalize(raw,refresh_bad.state).events[0].title=="Loaded"],
 ["loading and error presentation",contains(format(t.render(raw,null,started.state),'html'),"Loading timeline") and
    contains(format(t.render(raw,null,invalid.state),'html'),"could not be loaded")],
 ["custom empty presentation",contains(format(t.render({empty:<p "History is being written">}),'html'),"History is being written")],
 ["loaded source activates its first event",received.state.active_id=="a"],
 ["resource arrival emits activation",(received.notifications |: ~.name=="timeline_activate")[0].detail.value=="a"],
 ["subsequent actions resolve recorded source IDs",t.update(received.state,{type:"activate",id:"a"},context).state.active_id=="a"],
 ["resource refresh preserves activation",refreshed.state.active_id=="a"],
 ["data arrival notifies replacement",(received.notifications |: ~.name=="timeline_change")[0].detail.field=="data"]
]
