// resource requests carry identity/revision; rendering only consumes recorded results.
import c: .common
import schema: .schema
import adapters: .adapters
import util: lambda.chart.util
import story: .story

fn identity(value) => map([for (key,item in c.attrs(value) where string(key)!="priority") (key,item)])
fn same_resource(a, b) => identity(a)==identity(b)
fn source_descriptor(spec) => {id:"source",kind:"data",url:spec.source.url,format:c.option(spec.source,"format","json"),
    mapping:spec.source.mapping,priority:-1,timeout:c.option(spec.media_options,"timeout",30000)}

pub fn descriptors(spec, snapshot = null) {
    let items=story.items(spec,snapshot);
    let active=index_of(items |> ~.id,snapshot.active_id);
    let prefetch=c.option(spec.media_options,"prefetch",1);
    [if (spec.source.url!=null) source_descriptor(spec),
     for (index,item in items where spec.media_options.loading!="active" or index==active or
            active!=null and abs(index-active)<=prefetch)
        for (ordinal,media in item.media where media.src!=null)
            {id:util.binding_key(["media",item.id,c.option(media,"id",string(ordinal))]),kind:"media",url:media.src,
                event_id:item.id,media:media,timeout:c.option(spec.media_options,"timeout",30000),
                priority:if (index==active) 0 else if (active==null) index+1 else abs(index-active)}] |: ~!=null
}

// Revision and descriptor changes free slots; a timeout retires its token once.
pub fn lifecycle(spec, current, now = null) {
    let declared=descriptors(spec,current);
    let entries=[for (key,entry in c.attrs(current.resources),let descriptor=(declared |: ~.id==string(key))[0],
        let stale=entry.revision!=spec.source.revision or descriptor==null or not same_resource(entry.descriptor,descriptor),
        let expired=not stale and entry.status=="pending" and util.finite_number(now) and entry.deadline!=null and now>=entry.deadline)
        {id:string(key),entry:entry,stale:stale,expired:expired,
            error:if (expired) c.fail("resource.timeout","resource " ++ string(key) ++ " exceeded its deadline") else null}];
    let retired=entries |: ~.stale or ~.expired;
    {state:{*:current,resources:map([for (record in entries where not record.stale)
        (record.id,if (not record.expired) record.entry else {*:record.entry,status:"error",completed_at:now,result:null,error:record.error})])},
        requests:[for (record in retired where record.entry.status=="pending") {kind:"cancel_resource",id:record.id,
            token:record.entry.token,revision:record.entry.revision,reason:if (record.expired) "timeout" else "source_changed"}],
        notifications:[for (record in retired where record.expired) {name:"timeline_resource",detail:{chart_id:spec.id,
            id:record.id,token:record.entry.token,source_revision:spec.source.revision,error:record.error,origin:"deadline"}}]}
}
pub fn needs_clock(spec, current) => any([for (key,entry in c.attrs(current.resources))
    entry.status=="pending" and entry.deadline!=null or entry.status=="ready" and string(key)=="source" and spec.source.refresh!=null])
pub fn pending(spec, current, now = 0) {
    let usable=lifecycle(spec,current,now).state;
    let limit=c.option(spec.media_options,"concurrency",4);
    let running=len([for (key,entry in usable.resources where entry.status=="pending") key]);
    let candidates=sort([for (descriptor in descriptors(spec,current),
        let previous=usable.resources[descriptor.id],
        let retries=c.option(spec.media_options,"retry",0),
        let refresh=if (descriptor.kind=="data") spec.source.refresh else null,
        let expired=refresh is number and previous.status=="ready" and previous.completed_at!=null and now-previous.completed_at>=refresh
        where previous==null or not same_resource(previous.descriptor,descriptor) or previous.revision!=current.revision or expired or
            previous.status=="error" and previous.attempt<=retries) descriptor],{by:(entry)=>entry.priority});
    if (not c.integral(limit) or limit<1 or limit>1024) c.fail("media_options.concurrency","expected 1 to 1024")
    else slice(candidates,0,max(0,limit-running))
}
pub fn begin(current, descriptor, revision, now = 0) {
    let sequence=current.sequence+1;
    let token=util.binding_key(["resource",descriptor.id,string(sequence)]);
    let previous=current.resources[descriptor.id];
    let reusable=previous!=null and same_resource(previous.descriptor,descriptor) and previous.revision==revision;
    let entry={descriptor:descriptor,token:token,revision:revision,status:"pending",started_at:now,
        deadline:if (descriptor.timeout!=null and util.finite_number(now)) now+descriptor.timeout else null,
        attempt:if (reusable) previous.attempt+1 else 1,
        cached:reusable and (previous.status=="ready" or previous.cached==true),
        cached_result:if (reusable) (if (previous.status=="ready") previous.result else previous.cached_result) else null};
    {state:{*:current,sequence:sequence,resources:{*:current.resources,[descriptor.id]:entry}},
        request:{*:descriptor,kind:"resource",resource_kind:descriptor.kind,id:descriptor.id,token:token,revision:revision}}
}
pub fn complete(current, action, revision, now = 0, source = null) {
    let previous=current.resources[action.id];
    if (previous==null or previous.status!="pending" or action.token==null or action.token!=previous.token or
        action.revision!=revision or action.revision!=previous.revision or
        previous.deadline!=null and util.finite_number(now) and now>=previous.deadline) current else {
        // S7.9.1: error fields are outcomes; classify presence without comparing the error.
        let problem=if (not (action.error is null)) action.error else if (action.result is error) action.result else null;
        let completed={*:previous,status:if (not (problem is null)) "error" else "ready",completed_at:now,
            result:if (not (problem is null)) null else action.result,error:problem};
        let candidate={*:current,resources:{*:current.resources,[action.id]:completed}};
        let valid=if (problem is null and previous.descriptor.kind=="data" and source!=null) normalize(source,candidate) else null;
        if (not (valid is error)) candidate else {*:candidate,resources:{*:candidate.resources,[action.id]:{*:completed,status:"error",result:null,error:valid}}}
    }
}
pub fn normalize(raw, snapshot = null) {
    let authored=schema.authored(raw);
    let entry=snapshot.resources.source;
    let available=entry.status=="ready" or entry.cached==true;
    if (authored is error) authored else if (not available or entry.revision!=authored.source.revision or
        entry.descriptor!=null and not same_resource(entry.descriptor,source_descriptor(authored))) schema.validate(raw)
    else {
        let converted=adapters.decode(if (entry.status=="ready") entry.result else entry.cached_result,c.option(authored.source,"format","json"),
            if (authored.source.mapping!=null) {encoding:authored.source.mapping} else {});
        if (converted is error) converted else {
            let combined={*:converted,*:authored,data:converted.data,source:null,
                events:[*c.items(authored.events),*c.items(converted.events)],groups:[*c.items(authored.groups),*c.items(converted.groups)]};
            let normalized=schema.validate(combined);
            if (normalized is error) normalized else {*:normalized,source:c.attrs(authored.source)}
        }
    }
}

// callers may start this procedure as a structured task (S13.1.1v2–S13.1.5).
pub pn load(request, options = {}) {
    // io.fetch answers with a response map (AR-E1): the body is the resource, a non-2xx status a failure
    let response=io.fetch(request.url,{timeout:max(1,int(ceil(c.option(request,"timeout",30000)/1000.0))),*:options}) ^ { ^ };
    let problem=if (response is error) response else if (not response.ok) error("HTTP " ++ string(response.status)) else null;
    {type:"resource_result",id:request.id,token:request.token,revision:request.revision,
        result:if (problem is null) response.body else null,error:problem}
}
