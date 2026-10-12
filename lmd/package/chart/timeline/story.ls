// slideshow time is elapsed milliseconds, independent of historical coordinates.
import c: .common
import layout: .layout
import util: lambda.chart.util
import playback: lambda.chart.playback
import easing: lambda.slide.easing
import theme: .theme
import media_playback: .media_playback

pub fn items(spec, snapshot = null) {
    let shown=layout.visible(spec,snapshot).events;
    let order=spec.story.order;
    let ordered=if (order is array) [for (id in order) (shown |: ~.id==c.text(id))[0]] |: ~!=null else shown;
    [if (c.option(c.attrs(snapshot),"view",spec.view)=="story" and spec.intro!=null and spec.intro.visible!=false) spec.intro,*ordered] |: ~!=null
}
pub fn item(spec, id) => if (spec.intro!=null and spec.intro.id==id) spec.intro else (spec.events |: ~.id==id)[0]

pub fn plan(spec, snapshot = null) {
    if (spec is error) spec else {
    let events=items(spec,snapshot);
    let timing=spec.slideshow.duration;
    let durations=[for (item in events) if (item.duration!=null) item.duration else if (timing is fn) timing(item)
        else if (timing is map) c.option(timing,item.id,c.option(timing,"default",3000)) else if (timing!=null) timing else 3000];
    let repeat=c.option(spec.slideshow,"repeat",1);
    let direction=c.text(c.option(spec.slideshow,"direction","normal"));
    let curve=c.option(spec.slideshow,"easing",'linear');
    let named=if (curve is string) symbol(replace(curve,"_","-")) else curve;
    let failure=util.first_error([
        if (not all(durations |> c.positive(~))) c.fail("slideshow.duration","slide durations must be positive milliseconds"),
        if (repeat!="infinite" and repeat!=true and (not c.integral(repeat) or repeat<1)) c.fail("slideshow.repeat","expected a positive count or infinite"),
        if (not contains(["normal","reverse","alternate","reverse_alternate"],direction)) c.fail("slideshow.direction","invalid direction"),
        if (not (curve is fn) and not easing.valid(named)) c.fail("slideshow.easing","unknown easing function")]);
    if (failure is error) failure else {
        let length=sum(durations);
        let ends=math.cumsum(durations);
        let slides=[for (index,item in events) {id:item.id,event:item,at:if (index==0) 0 else ends[index-1],duration:durations[index],spec:{active_id:item.id}}];
        {slides:slides,keyframes:[*slides,{at:length,spec:{active_id:events[len(events)-1].id}}],
            length:length,duration:length,repeat:if (repeat==true) "infinite" else repeat,direction:direction,fill:"forwards",
            autoplay:spec.slideshow.autoplay==true,delay:c.option(spec.slideshow,"delay",0),easing:named,
            transition:c.text(c.option(spec.slideshow,"transition","none")),transition_duration:c.option(spec.slideshow,"transition_duration",300),
            motion:spec.motion,source_revision:spec.source.revision,media_end:spec.slideshow.media_end==true,media_timeout:c.option(spec.slideshow,"media_timeout",30000)}
    }
    }
}
pub fn sample(plan, elapsed) {
    if (plan is error) plan else if (not util.finite_number(elapsed) or elapsed<0) c.fail("slideshow.time","expected nonnegative elapsed milliseconds")
    else if (len(plan.slides)==0) {active_id:null,index:null,progress:0,overall_progress:0,complete:true}
    else {
        let position=playback.position(plan,elapsed);
        let candidates=[for (index,slide in plan.slides where
            if (position.reversed and position.at>0) slide.at<position.at else slide.at<=position.at) index];
        let index=if (len(candidates)>0) candidates[len(candidates)-1] else 0;
        let slide=plan.slides[index];
        let fraction=util.clamp_val((position.at-slide.at)/slide.duration,0,1);
        let waiting=elapsed<plan.delay;
        let progress=if (waiting) 0 else if (position.reversed) 1-fraction else fraction;
        let raw=if (waiting or position.complete or plan.motion!="full" or plan.transition=="none" or plan.transition_duration==0) 1.0
            else util.clamp_val(progress*slide.duration/min(slide.duration,plan.transition_duration),0,1);
        let eased=if (plan.easing is fn) plan.easing(raw) else easing.sample(plan.easing,raw);
        let previous_index=index+(if (position.reversed) 1 else -1);
        let previous_reverse=contains(["reverse","reverse_alternate"],plan.direction)!=
            (contains(["alternate","reverse_alternate"],plan.direction) and (position.cycle-1)%2==1);
        let outgoing=if (previous_index>=0 and previous_index<len(plan.slides)) plan.slides[previous_index].id
            else if (position.cycle>0) plan.slides[if (previous_reverse) 0 else len(plan.slides)-1].id else null;
        if (eased is error) eased else if (not util.finite_number(eased) or eased<0 or eased>1) c.fail("slideshow.easing","expected finite progress from zero to one") else
        {active_id:slide.id,index:index,progress:progress,waiting:waiting,
            transition:{kind:plan.transition,progress:eased,reversed:position.reversed,
                outgoing_id:if (raw<1 and not waiting and outgoing!=slide.id) outgoing else null},
            overall_progress:if (plan.length==0) 0 else if (position.reversed) 1-position.at/plan.length else position.at/plan.length,
            complete:position.complete,cycle:position.cycle}
    }
}
pub fn command(current, action, now, spec) {
    let timeline=plan(spec,current);
    let before=current.playback;
    let op=c.text(action.type);
    let next=if (len(timeline.slides)==0) {*:before,phase:"ended",elapsed:0,anchor:null,position:0}
        else if (op=="stop") {*:before,phase:"idle",elapsed:0,anchor:null,position:0}
        else playback.command({reversed:false,*:c.attrs(before)},
            {command:if (op=="tick") "frame" else op,position:c.option(action,"time_ms",action.position)},now,timeline);
    if (timeline is error) timeline else if (next is error) next else {
        let initial_frame=sample(timeline,next.elapsed);
        let previous=if (current.story_frame!=null) current.story_frame else sample(timeline,before.elapsed);
        let guarded=if (initial_frame is error) initial_frame else if (previous is error) previous
            else media_playback.guard(current,next,initial_frame,previous,timeline,now,op);
        let settled={*:guarded.playback,generation:c.option(before,"generation",0)+(if (contains(["play","seek","stop"],op)) 1 else 0)};
        let slide=if (guarded.holding) guarded.frame else sample(timeline,settled.elapsed);
        if (guarded is error) guarded else if (slide is error) slide else {state:{*:current,playback:settled,media_wait:guarded.wait,
            media_stopped:if (op=="stop") true else if (op=="play") false else current.media_stopped,
            active_id:slide.active_id,story_frame:slide},requests:[],
            notifications:[*(if (settled.phase!=before.phase or slide.active_id!=current.active_id)
                [{name:"timeline_playback",detail:{chart_id:spec.id,phase:settled.phase,elapsed:settled.elapsed,active_id:slide.active_id,progress:slide.progress,
                    source_revision:spec.source.revision,origin:c.option(action,"origin","programmatic")}}] else []),
                *(if (guarded.timed_out) [{name:"timeline_media_timeout",detail:{chart_id:spec.id,event_id:current.active_id,
                    token:current.media_session.token,source_revision:spec.source.revision}}] else [])]}
    }
}

// Manual navigation resets the retained playhead before the next clock tick.
pub fn align(current, id, now, spec) {
    if (not contains(["playing","paused"],current.playback.phase)) {*:current,story_frame:null}
    else {
        let timeline=plan(spec,current);
        let slide=(timeline.slides |: ~.id==id)[0];
        let changed=if (timeline is error) timeline else if (slide==null) null else command(current,{type:"seek",time_ms:slide.at},now,spec);
        if (changed is error) changed else if (changed==null) {*:current,story_frame:null} else changed.state
    }
}

// Keep the visible slide fraction and cycle when source order or durations change.
pub fn rebase(previous, current, previous_spec, spec, now = null) {
    if (not contains(["playing","paused"],previous.playback.phase)) {*:current,story_frame:null,media_wait:null}
    else {
        let before=plan(previous_spec,previous);
        let after=plan(spec,current);
        let clock=if (before is error) before else if (before.length==0) previous.playback
            else playback.command(previous.playback,{command:"frame"},now,before);
        let position=if (clock is error) clock else if (before.length==0) {at:0,cycle:0,reversed:false}
            else playback.position(before,clock.elapsed);
        let from=(before.slides |: ~.id==previous.active_id)[0];
        let to=(after.slides |: ~.id==current.active_id)[0];
        let fraction=if (from==null or previous.active_id!=current.active_id) 0 else if (previous.media_wait!=null) 1
            else util.clamp_val((position.at-from.at)/from.duration,0,1);
        let at=if (to==null) 0 else to.at+fraction*to.duration;
        let cycle_start=after.delay+c.option(position,"cycle",0)*after.length;
        let destination=if (after.length==0) {reversed:false} else playback.position(after,cycle_start);
        let elapsed=cycle_start+(if (destination.reversed==true) after.length-at else at);
        let shifted={*:current.playback,elapsed:elapsed,anchor:if (current.playback.phase=="playing") now else null};
        let settled=if (len(after.slides)==0) {*:shifted,phase:"ended",elapsed:0,anchor:null}
            else playback.command(shifted,{command:"frame"},now,after);
        let failure=util.first_error([before,after,clock,position,settled]);
        if (failure is error) failure else {*:current,playback:{*:settled,anchor:if (settled.phase=="playing") now else null},
            story_frame:sample(after,settled.elapsed),media_wait:null}
    }
}
pub fn frame(spec, snapshot = null) {
    let sampled=if (snapshot.story_frame!=null and snapshot.story_frame.active_id==snapshot.active_id) snapshot.story_frame
        else if (snapshot.playback.elapsed!=null) sample(plan(spec,snapshot),snapshot.playback.elapsed) else null;
    if (sampled is error) sampled else if (snapshot.active_id==null or sampled.active_id==snapshot.active_id) sampled else null
}
pub fn style(spec, snapshot = null, outgoing = false) {
    let transition=frame(spec,snapshot).transition;
    let progress=transition.progress;
    if (transition==null or spec.motion!="full" or snapshot.playback.phase=="idle" and snapshot.sample_time_ms==null) ""
    else if (transition.kind=="fade") "opacity:" ++ c.text(if (outgoing) 1-progress else progress) ++ ";"
    else if (transition.kind=="slide") "transform:translateX(" ++
        c.text((if (outgoing) -progress else 1-progress)*(if (transition.reversed) -24 else 24)) ++ "px);"
    else if (transition.kind=="reveal") if (outgoing) "" else "clip-path:inset(0 " ++ c.text((1-progress)*100) ++ "% 0 0);" else ""
}
pub fn indicators(spec, snapshot = null) {
    let sampled=frame(spec,snapshot);
    [for (kind in ["progress","overall_progress"] where spec.slideshow[kind]==true)
        <progress *:theme.part(spec,kind,{class:"lambda-timeline-" ++ replace(kind,"_","-"),max:"1",value:c.text(c.option(c.attrs(sampled),kind,0)),
            'aria-label':c.message(spec,kind,if (kind=="progress") "Current event progress" else "Overall timeline progress")})>]
}
