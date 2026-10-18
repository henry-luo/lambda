// Player handles belong to retained DOM view state; script state retains only session identities.
import dom
import c: .common
import util: lambda.chart.util
import events: lambda.chart.events
import reducer: .state
import media: .media

fn key(value) => util.binding_key([value.event_id,value.id])
fn live(value, current) => current.media_session!=null and value.token==current.media_session.token and
    value.revision==current.media_session.revision and value.event_id==current.media_session.event_id
fn result(value, status) => {type:"media_event",event_id:value.event_id,id:value.id,
    token:value.token,revision:value.revision,status:status,origin:"native"}
fn player(host, value) {
    [for (node in dom.query_selector_all(host,"[data-timeline-player]") where
        events.attribute(node,"data-timeline-player-event")==value.event_id and
        events.attribute(node,"data-timeline-player-id")==value.id) node][0]
}

pub pn step(host, clock, current, render, enabled = true) {
    var bindings=c.attrs(clock.native_players);
    var actions=[];
    if (render and enabled) {
        for (request in c.items(clock.native_media_requests) where media.identify(request.media).provider==null) {
            let id=key(request);
            let previous=bindings[id];
            let node=player(host,request);
            if (request.command=="play" and live(request,current) and current.media_session.phase=="playing") {
                let fresh=previous==null or previous.token!=request.token or previous.revision!=request.revision;
                let started=node!=null and dom.media_command(node,"play",if (fresh) c.option(request.media,"start",0) else null);
                bindings={*:bindings,[id]:{*:request,phase:"playing",complete:not started}};
                if (not started) { actions=[*actions,result(request,"error")] }
            } else if (request.command=="pause" and previous!=null and previous.token==request.token and previous.revision==request.revision) {
                if (node!=null) { dom.media_command(node,"pause",null) }
                bindings={*:bindings,[id]:{*:previous,phase:"paused"}}
            }
        }
    }
    // A bookmark or source replacement can retire a session before its queued pause reaches render.
    for (id,binding in bindings where not enabled or not live(binding,current)) {
        let node=player(host,binding);
        if (node!=null) { dom.media_command(node,"pause",null) }
    }
    bindings=map([for (id,binding in bindings where enabled and live(binding,current)) (id,binding)]);
    for (id,binding in bindings where binding.phase=="playing" and binding.complete!=true) {
        let node=player(host,binding);
        let info=if (node==null) null else dom.media_info(node);
        let ended=info.state=="ended" or info.current_time!=null and binding.media.end!=null and info.current_time>=binding.media.end;
        let failed=info==null or info.state=="error";
        if (ended or failed) {
            if (node!=null) { dom.media_command(node,"pause",null) }
            bindings={*:bindings,[id]:{*:binding,complete:true}};
            actions=[*actions,result(binding,if (failed) "error" else "ended")]
        }
    }
    {bindings:bindings,actions:actions}
}

pub fn needs_clock(bindings) => any([for (id,binding in c.attrs(bindings)) binding.phase=="playing" and binding.complete!=true])
pub fn queued(requests) => [for (request in requests where request.kind=="media_command" and media.identify(request.media).provider==null) request]
pub pn reduce(current, actions, spec, now) {
    var folded=reducer.result(current);
    for (action in actions) {
        let next=reducer.update(folded.state,action,{normalized_spec:spec,revision:spec.source.revision,now:now});
        if (next is error) { return next }
        folded={*:next,requests:[*folded.requests,*next.requests],notifications:[*folded.notifications,*next.notifications]}
    }
    folded
}
