import slide: lambda.slide
import fx: lambda.slide.transitions
import clock: lambda.slide.clock
import media: lambda.doc.media
import m: ~~.~~.demo.slides.mod_model
import ui_model: ~~.~~.demo.slides.mod_transitions

fn deck(kind,variant=null,direction='left') => <presentation width:400.0,height:240.0,
    <slide id:"before",background:"#ff0000">
    <slide id:"after",background:"#0000ff",transition:kind,transition_variant:variant,direction:direction,transition_duration:1000.0>>
fn plan(kind,variant=null,direction='left') => slide.compile(deck(kind,variant,direction))^
fn sample(kind,variant=null,direction='left',elapsed=500.0) => fx.sample(plan(kind,variant,direction),{slide:1,phase:'transition',time_ms:elapsed})
let wipe = sample('wipe')
let reveal = sample('reveal')
let split = sample('split','in')
let bars = sample('random-bars')
let shape = sample('shape')
let chosen = ui_model.choose({*:m.initial(),index:1},"split")^
let configured = ui_model.property(ui_model.property(ui_model.property(chosen,"duration","1.2")^,"variant","in")^,"automatic",true)^
let all_slides = ui_model.apply_all(configured)^
let restored = m.initial(parse(format(all_slides.doc,'mark')^,'mark')^)
let preview = slide.compile(ui_model.preview({*:all_slides,index:0}))^
let timed_plan = slide.compile(<presentation
    <slide id:"a",advance_on_click:false,advance_after:200.0,<text id:"text","A">
        <cue start:'entry',<effect target:"text",kind:'fade-in',duration:100.0>>>
    <slide id:"b",advance_after:300.0,transition:'wipe',transition_duration:100.0>
    <slide id:"c">>)^
fn command(st,key,at) => slide.reduce(timed_plan,st,{command:key,time_ms:at})^
let active = command(slide.initial_state(timed_plan),"activate",0.0)
let built = command(active,"frame",100.0)
let paused = command(built,"pause",150.0)
let resumed = command(paused,"resume",1000.0)
let during = command(resumed,"frame",1150.0)
let arrived = command(during,"frame",1250.0)
let final = command(arrived,"frame",1550.0)
let faster = slide.reduce(timed_plan,built,{command:"speed",rate:2.0,time_ms:200.0})^
let sought = slide.reduce(timed_plan,slide.initial_state(timed_plan),{command:"seek_time",position_ms:250.0,time_ms:1000.0})^
let composed = m.initial(<presentation master:"base",
    <layout id:"custom",<placeholder role:"body",x:30.0,y:30.0,width:500.0,height:300.0>>
    <master id:"base",transition:'wipe',advance_after:150.0,<text id:"title","Shared">>
    <slide layout:"custom",<text id:"body",role:'body',"Text">>>)
let composed_preview = slide.compile(ui_model.preview(composed))^
let instant = slide.compile(<presentation <slide advance_after:0.0><slide advance_after:0.0><slide>> )^
let instant_start = slide.reduce(instant,slide.initial_state(instant),{command:"activate",time_ms:0.0})^;
{
    catalog:[for (entry in fx.catalog) fx.descriptor(entry.id) != null and slide.compile(deck(entry.id))^.slides[1].transition == entry.id],
    visual:[wipe.incoming.clip == "inset(0% 0% 0% 50%)",reveal.outgoing.z > reveal.incoming.z,
        abs(sample('push').outgoing.tx+200.0)<0.001,sample('cover').outgoing.tx == 0.0,abs(sample('uncover').outgoing.tx+200.0)<0.001,
        starts_with(split.incoming.clip,"path("),starts_with(bars.incoming.clip,"path("),starts_with(shape.incoming.clip,"circle("),
        starts_with(sample('shape','diamond').incoming.clip,"polygon("),sample('fade','through-black').incoming.opacity == 0.0,
        sample('fade','through-black').outgoing.opacity < 0.001,contains(fx.style(wipe.incoming),"clip-path:inset("),
        fx.sample(plan('wipe'),{slide:1,phase:'waiting',time_ms:0.0}).incoming.clip == "none",
        sample('wipe',null,'right').incoming.clip == "inset(0% 50% 0% 0%)"],
    editing:[ui_model.choose(configured,"split")^.doc == configured.doc,len(configured.undo) == len(chosen.undo)+3,all([for (scene in all_slides.plan.slides) scene.transition == 'split' and scene.transition_variant == 'in' and scene.transition_duration == 1200.0 and scene.advance_after == 2000.0]),
        len(all_slides.undo) == len(configured.undo)+1,m.history(all_slides).doc == configured.doc,restored.doc == all_slides.doc,
        preview.slides[1].transition == 'split' and len(preview.slides[1].cues) == 0],
    timing:[active.phase == 'cue',built.timed and not built.autoplay,paused.wait_ms == 50.0,
        command(paused,"frame",900.0).wait_ms == 50.0,during.slide == 1 and during.phase == 'transition',
        arrived.timed and arrived.slide == 1,final.slide == 2 and not slide.needs_frame(final),
        clock.timeline(timed_plan).duration_ms == 700.0,
        slide.reduce(instant,instant_start,{command:"frame",time_ms:0.0})^.slide == 2],
    lifecycle:[not command(built,"previous",150.0).timed,
        command(faster,"frame",250.0).slide == 1,
        command(command(paused,"play",1000.0),"frame",1150.0).slide == 1,
        sought.timed and clock.position(timed_plan,sought) == 250.0,
        command(sought,"frame",1050.0).slide == 1,
        not slide.needs_frame(command(during,"stop",1200.0)),
        slide.initial_state(timed_plan,{start_slide:1}).slide == 1,
        composed.plan.slides[0].advance_after == 150.0 and len(composed_preview.slides[1].objects) == 2],
    media:[media.data_uri_bytes(media.data_uri(b'01FF',"audio/wav")^)^ == b'01FF',
        media.data_uri_bytes("data:audio/wav,plain") ^ {true} ~ {false}],
    invalid:[slide.compile(deck('bad')) ^ {true} ~ {false},slide.compile(deck('split','circle')) ^ {true} ~ {false},
        slide.compile(<presentation <slide advance_after: -1.0>>) ^ {true} ~ {false},
        slide.compile(<presentation <slide advance_on_click:"false">>) ^ {true} ~ {false},
        slide.compile(<presentation <slide transition_sound:{src:"",name:"Bad"}>>) ^ {true} ~ {false},
        ui_model.property(chosen,"duration","nan") ^ {true} ~ {false},ui_model.property(chosen,"after","-2") ^ {true} ~ {false}]
}
