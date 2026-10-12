// Player requests use revision/generation tokens; the pure clock has a bounded wait.
import c: .common
import util: lambda.chart.util
import media: .media
import playback: lambda.chart.playback

fn active(spec, id) => if (spec.intro!=null and spec.intro.id==id) spec.intro else (spec.events |: ~.id==id)[0]
fn players(item) => [for (index,record in c.items(item.media) where contains(["video","audio"],media.identify(record).kind))
    {id:c.text(c.option(record,"id",string(index))),media:record}]
fn requests(session, command) => [for (player in c.items(session.players)) {kind:"media_command",command:command,
    event_id:session.event_id,id:player.id,media:player.media,token:session.token,revision:session.revision}]
pub fn finished(session) => session==null or all([for (player in c.items(session.players))
    contains(c.items(session.completed),player.id) or contains(c.items(session.failed),player.id)])

pub fn synchronize(current, spec, now) {
    let old=current.media_session;
    let shown=players(active(spec,current.active_id));
    let enabled=spec.slideshow.media_end==true or spec.media_options.active_playback==true;
    let phase=if (current.playback.phase=="idle" and spec.media_options.active_playback==true and current.media_stopped!=true)
        "playing" else current.playback.phase;
    let valid=old!=null and old.event_id==current.active_id and old.revision==spec.source.revision and
        old.generation==c.option(current.playback,"generation",0) and old.cycle==current.story_frame.cycle and old.players==shown;
    let creating=enabled and len(shown)>0 and phase=="playing" and not valid;
    let retiring=old!=null and (not valid or not enabled or contains(["idle","ended"],phase));
    let session=if (creating) {event_id:current.active_id,players:shown,token:current.sequence+1,
        revision:spec.source.revision,generation:c.option(current.playback,"generation",0),
        cycle:current.story_frame.cycle,phase:phase,completed:[],failed:[],started_at:now}
        else if (retiring) null else if (valid) {*:old,phase:phase} else old;
    let phase_changed=valid and session!=null and old.phase!=session.phase;
    {state:{*:current,media_session:session,sequence:if (creating) session.token else current.sequence},notifications:[],
        requests:[*requests(if (retiring and spec.media_options.pause_on_deactivate!=false) old else null,"pause"),
            *requests(if (creating) session else null,"play"),
            *requests(if (phase_changed and not finished(session)) session else null,if (session.phase=="playing") "play" else "pause")]}
}

pub fn accept(current, action, spec) {
    let session=current.media_session;
    if (session==null or session.token!=action.token or session.event_id!=action.event_id or
        session.revision!=action.revision or action.revision!=spec.source.revision or
        not contains(session.players |> ~.id,c.text(action.id))) current
    else if (not contains(["ended","error"],c.text(action.status))) c.fail("media.status","expected ended or error")
    else {*:current,media_session:{*:session,
        completed:if (action.status=="ended") unique([*session.completed,c.text(action.id)]) else session.completed,
        failed:if (action.status=="error") unique([*session.failed,c.text(action.id)]) else session.failed}}
}

pub fn guard(current, next, frame, before_frame, plan, now, operation) {
    let reset=contains(["play","seek","stop"],operation);
    let old=if (reset) null else current.media_wait;
    let session=if (reset or current.media_session.revision!=plan.source_revision) null else current.media_session;
    let elapsed=if (old==null) 0 else old.elapsed + (if (current.playback.phase=="playing" and
        util.finite_number(now) and util.finite_number(old.anchor)) max(0,now-old.anchor) else 0);
    let timeout=c.option(plan,"media_timeout",30000);
    let boundary=if (old!=null) old.boundary else max(current.playback.elapsed,plan.delay)+
        (1-c.option(before_frame,"progress",0))*plan.slides[before_frame.index].duration;
    let crossed=old!=null or before_frame.active_id!=frame.active_id or frame.complete;
    let holding=not reset and plan.media_end==true and session!=null and session.event_id==current.active_id and
        not finished(session) and crossed and elapsed<timeout and contains(["playing","paused"],current.playback.phase);
    let wait=if (not holding) null else {event_id:current.active_id,boundary:boundary,elapsed:elapsed,anchor:now};
    let settled=if (holding) {*:next,phase:if (operation=="pause") "paused" else if (operation=="resume") "playing" else current.playback.phase,
        elapsed:boundary,anchor:if (operation=="pause" or current.playback.phase=="paused" and operation!="resume") null else now}
        else if (old!=null) {*:next,elapsed:old.boundary,
            phase:if (playback.position(plan,old.boundary).complete) "ended" else if (operation=="pause") "paused"
                else if (operation=="resume") "playing" else current.playback.phase,
            anchor:if (next.phase=="playing") now else null} else next;
    {playback:settled,wait:wait,holding:holding,frame:if (holding) {*:before_frame,active_id:current.active_id,progress:1,
        transition:{*:before_frame.transition,progress:1},complete:false,waiting_media:true} else frame,
        timed_out:old!=null and elapsed>=timeout and not finished(session)}
}
