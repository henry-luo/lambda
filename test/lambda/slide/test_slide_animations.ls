import slide: lambda.slide
import catalog: lambda.slide.animation
import sample: lambda.slide.sample
import html: lambda.slide.html
import c: lambda.slide.common
import m: ~~.~~.demo.slides.mod_model
import a: ~~.~~.demo.slides.mod_animations

fn deck(effects) => <presentation width:400.0,height:240.0,<slide
    <shape id:"box",x:20.0,y:20.0,width:100.0,height:80.0,fill:"#ff0000",stroke:"#000000",stroke_width:4.0>
    <shape id:"button",x:220.0,y:20.0,width:80.0,height:80.0,fill:"#00ff00">;*effects>>
fn compile(effects) map^ => slide.compile(deck(effects))^
let choices = [for (entry in catalog.catalog) {
    let plan = compile([<cue <effect *:catalog.defaults(entry.id),target:"box">>])^;
    plan.slides[0].cues[0].duration_ms >= 0.0
}]
let timing = compile([
    <cue id:"first",<effect target:"box",kind:'fade-in',duration:100.0>>,
    <cue id:"with",start:'with-previous',<effect target:"button",kind:'spin',delay:50.0,duration:200.0>>,
    <cue id:"after",start:'after-previous',<effect target:"box",kind:'grow',duration:100.0,scale:2.0>>])^
let timed_mid = sample.scene(timing.slides[0],0,300.0)
let trigger_plan = compile([
    <cue trigger:"button",<effect target:"box",kind:'fade-in',duration:200.0>>,
    <cue <effect target:"button",kind:'spin',duration:1000.0>>])^
fn event(ps,command,at) => slide.reduce(trigger_plan,ps,{command:command,time_ms:at})^
let initial = slide.initial_state(trigger_plan)
let fired = slide.reduce(trigger_plan,initial,{command:"trigger",target:"button",time_ms:0.0})^
let halfway = event(fired,"frame",100.0)
let paused = event(halfway,"pause",120.0)
let still = event(paused,"frame",800.0)
let resumed = event(still,"resume",1000.0)
let sped = slide.reduce(trigger_plan,resumed,{command:"speed",rate:2.0,time_ms:1020.0})^
let finished = event(sped,"frame",1050.0)
let replayed = slide.reduce(trigger_plan,finished,{command:"trigger",target:"button",time_ms:1100.0})^
let stopped = event(replayed,"stop",1120.0)
let base = {*:m.initial(deck([])),selected:"s0-o0"}
let added = a.choose(base,'fade-in')^
let replaced = a.choose(added,'blinds-in')^
let layered = a.choose(replaced,'spin',true)^
let delayed = a.property(layered,"delay","0.2")^
let started = a.property(delayed,"start","after-previous")^
let triggered = a.property(started,"trigger","button")^
let painted = a.paint({*:triggered,selected:"s0-o1",animation_clipboard:a.copy(triggered)})^
let removed = a.remove(painted)^
let anonymous = {*:m.initial(<presentation <slide <text "No ID">>>),selected:"s0-o0"}
let pinned = a.choose(anonymous,'appear')^
let deleted_trigger = m.remove_object({*:triggered,selected:"s0-o1"})^
let outline = compile([<cue <effect target:"box",kind:'line-color',color:"#ffffff",duration:100.0>>])^
let outlined = sample.scene(outline.slides[0],0,50.0)[0]
let masks = [for (kind in ['blinds-in','checkerboard-in','dissolve-in']) {
    let plan = compile([<cue <effect target:"box",kind:kind,duration:100.0>>])^
    let mid = sample.scene(plan.slides[0],0,50.0)[0];
    starts_with(html.effect_properties(mid)["clip-path"],"path(") and html.effect_properties(sample.scene(plan.slides[0],0,100.0)[0])["clip-path"] == "none"
}]
let stagger = compile([
    <cue <effect target:"box",kind:'spin',duration:1000.0>>,
    <cue start:'with-previous',<effect target:"button",kind:'grow',scale:2.0,duration:100.0>>,
    <cue start:'after-previous',<effect target:"button",kind:'transparency',duration:200.0,opacity:0.5>>])^
let reordered_paints = compile([
    <cue <effect target:"box",kind:'highlight',color:"#0000ff",delay:500.0,duration:100.0>>,
    <cue start:'with-previous',<effect target:"box",kind:'highlight',color:"#00ff00",duration:100.0>>])^
let simultaneous = compile([
    <cue id:"one",trigger:"button",<effect target:"box",kind:'fade-in',duration:100.0>>,
    <cue id:"two",trigger:"box",<effect target:"button",kind:'spin',duration:200.0>>])^
let one = slide.reduce(simultaneous,slide.initial_state(simultaneous),{command:"trigger",target:"button",time_ms:0.0})^
let two = slide.reduce(simultaneous,one,{command:"trigger",target:"box",time_ms:50.0})^
let both = slide.reduce(simultaneous,two,{command:"frame",time_ms:150.0})^
let sought = slide.reduce(simultaneous,both,{command:"seek_time",position_ms:0.0,time_ms:200.0})^
let imported = {*:m.initial(deck([<cue id:"group",<sequence
    <effect target:"box",kind:'spin',duration:100.0><effect target:"box",kind:'grow',duration:100.0,scale:2.0>>>])),selected:"s0-o0"}
let second = a.select(imported,"0.0.1")
let edited_group = a.property(second,"duration","0.3")^
let colored = a.choose(base,'highlight')^
let copied_twice = a.paint({*:painted,selected:"s0-o1",animation_clipboard:a.copy(triggered)})^
let preceding_removed = m.remove_object({*:m.initial(deck([
    <cue <effect target:"box",kind:'spin'>>,
    <cue start:'after-previous',<effect target:"button",kind:'spin'>>])),selected:"s0-o0"})^
let after_plan = slide.compile(<presentation
    <slide advance_after:1000.0,<shape id:"box"><shape id:"button">
        <cue trigger:"button",<effect target:"box",kind:'spin',duration:100.0>>>
    <slide>>)^
let after_active = slide.reduce(after_plan,slide.initial_state(after_plan),{command:"activate",time_ms:0.0})^
let after_fired = slide.reduce(after_plan,after_active,{command:"trigger",target:"button",time_ms:100.0})^
let after_done = slide.reduce(after_plan,after_fired,{command:"frame",time_ms:200.0})^
let after_advanced = slide.reduce(after_plan,after_done,{command:"frame",time_ms:1200.0})^
fn invalid(items) => compile(items) ^ {true} ~ {false};
{
    catalog:choices,
    timing:[len(timing.slides[0].cues)==1,timing.slides[0].cues[0].duration_ms==350.0,
        timed_mid[0].sx==1.5,timed_mid[1].rotation==360.0,sample.address(timing,{cue:"after"})^.cue==0,
        sample.scene(stagger.slides[0],0,200.0)[1].opacity==0.75,
        sample.scene(reordered_paints.slides[0],0,550.0)[0].paint=="rgba(0,127.5,127.5,1)"],
    triggers:[len(trigger_plan.slides[0].cues)==1,len(trigger_plan.slides[0].triggers)==1,
        slide.sample(trigger_plan,initial)^[0].visible==0.0,slide.sample(trigger_plan,halfway)^[0].opacity==0.5,
        still.triggered[0].time_ms==paused.triggered[0].time_ms,finished.triggered[0].done,
        not slide.needs_frame(finished),replayed.triggered[0].time_ms==0.0,not slide.needs_frame(stopped),
        event(replayed,"restart-slide",1200.0).triggered==[],
        len(both.triggered)==2,both.triggered[0].done,not both.triggered[1].done,sought.triggered==[],
        after_active.timed,not after_fired.timed,after_done.timed,after_advanced.slide==1],
    authoring:[len(added.undo)==1,len(a.entries(replaced))==1,a.selected(replaced).effect.kind=='blinds-in',
        len(a.entries(layered))==2,a.selected(delayed).effect.delay==200.0,
        len(m.scene(started).cues)==1,len(m.scene(triggered).triggers)==1,
        len(a.entries(painted))==4,len(a.entries(removed))==3,m.history(removed).doc==painted.doc,
        m.initial(parse(format(painted.doc,'mark'),'mark')^).doc==painted.doc,
        len(pinned.undo)==1,m.chosen(pinned).source.id!=null,m.history(pinned).doc==anonymous.doc,
        len(m.scene(deleted_trigger).triggers)==0,len(slide.compile(a.preview(triggered))^.slides)==1,
        len(a.entries(copied_twice))==6,name(a.cues(edited_group)[0][0])=='sequence',m.scene(edited_group).cues[0].duration_ms==400.0,
        a.selected(edited_group).effect.duration==300.0,
        len(m.scene(preceding_removed).cues)==1,m.scene(preceding_removed).cues[0].start=='click',
        a.selected(a.choose(a.choose(base,'appear')^,'spin')^).effect.duration>0.0],
    paint:[outlined.paint=="#ff0000",outlined.stroke=="rgba(127.5,127.5,127.5,1)"],masks:masks,
    invalid:[invalid([<cue start:'after-previous',<effect target:"box",kind:'spin'>>]),
        invalid([<cue trigger:"missing",<effect target:"box",kind:'appear'>>]),
        invalid([<cue <effect id:"duplicate",target:"box",kind:'spin'><effect id:"duplicate",target:"button",kind:'spin'>>]),
        invalid([<cue <effect target:"box",kind:'spin',duration:100.0>>,
            <cue start:'with-previous',<effect target:"box",kind:'spin',duration:100.0>>]),
        a.property(added,"duration","-1") ^ {true} ~ {false},
        a.property(added,"repeat","1.5") ^ {true} ~ {false},
        a.property(colored,"color","not-a-color") ^ {true} ~ {false}]
}
