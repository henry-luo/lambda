import t: lambda.chart.timeline
import story: lambda.chart.timeline.story
let raw={id:"story",view:"story",spacing:"sequence",slideshow:{duration:100,repeat:2,direction:"alternate"},
    events:[{id:"a",title:"First"},{id:"b",title:"Second",duration:200},{id:"c",title:"Third"}]}
let spec=t.validate(raw)
let plan=story.plan(spec)
let initial=t.initial_state(raw)
let played=t.update(initial,{type:"play"},{spec:raw,now:1000})
let ticked=t.update(played.state,{type:"tick"},{spec:raw,now:1150})
let paused=t.update(ticked.state,{type:"pause"},{spec:raw,now:1200})
let resumed=t.update(paused.state,{type:"resume"},{spec:raw,now:2000})
let advanced=t.update(resumed.state,{type:"tick"},{spec:raw,now:2150})
let sought=t.update(initial,{type:"seek",time_ms:320},{spec:raw,now:0})
let controlled={*:raw,value:{active_id:"a"}}
let request=t.update(t.initial_state(controlled),{type:"seek",time_ms:150},{spec:controlled,now:0})
let delayed_raw={*:raw,slideshow:{duration:100,delay:50,transition:"fade",transition_duration:40,progress:true,overall_progress:true}}
let delayed=story.plan(t.validate(delayed_raw))
let entering=story.sample(delayed,70)
let delayed_play=t.update(t.initial_state(delayed_raw),{type:"play"},{spec:delayed_raw,now:1000})
let delayed_tick=t.update(delayed_play.state,{type:"tick"},{spec:delayed_raw,now:1070})
let frozen=t.render_frame(delayed_raw,70,{width:800,height:400})
let reverse=story.plan(t.validate({*:delayed_raw,slideshow:{duration:100,direction:"reverse",transition:"slide"}}));
let manual=t.update(played.state,{type:"activate",id:"b"},{spec:raw,now:1050});
let fade_raw={*:raw,slideshow:{duration:100,repeat:2,transition:"fade",transition_duration:40}};
let fade_spec=t.validate(fade_raw);
let fade_plan=story.plan(fade_spec);
let fade_sample=story.sample(fade_plan,120);
let fade_html=format(t.render_frame(fade_raw,120,{width:800,height:400}),'html');
let media_html=format(t.render_frame({*:fade_raw,events:[{id:"a",title:"Video",media:[{src:"movie.mp4",poster:"poster.png"}]},{id:"b",title:"Next"}]},120,{width:800,height:400}),'html');

[
 ["per slide timing",plan.length==400 and plan.slides[1].at==100 and plan.slides[2].at==300],
 ["start",story.sample(plan,0).active_id=="a"],
 ["boundary",story.sample(plan,100).active_id=="b"],
 ["progress",story.sample(plan,200).progress==0.5],
 ["alternate cycle",story.sample(plan,450).active_id=="c" and story.sample(plan,450).cycle==1],
 ["complete",story.sample(plan,800).complete and story.sample(plan,800).active_id=="a"],
 ["play",played.state.playback.phase=="playing" and played.state.playback.anchor==1000],
 ["tick",ticked.state.active_id=="b" and ticked.state.playback.elapsed==150],
 ["pause",paused.state.playback.phase=="paused" and paused.state.playback.elapsed==200],
 ["resume keeps elapsed",advanced.state.playback.elapsed==350 and advanced.state.active_id=="c"],
 ["seek",sought.state.active_id=="c" and sought.state.playback.elapsed==320],
 ["stop",t.update(advanced.state,{type:"stop"},{spec:raw,now:3000}).state.playback.phase=="idle"],
 ["controlled slideshow requests",request.state.active_id=="a" and request.requests[0].field=="active_id" and request.requests[0].value=="b"],
 ["frozen render",t.render_frame(raw,150,{width:800,height:400}) is element],
 ["empty story",story.sample(story.plan(t.validate({events:[]})),0).complete],
 ["invalid duration",story.plan(t.validate({*:raw,slideshow:{duration:0}})) is error],
 ["invalid order",story.plan(t.validate({*:raw,story:{order:["unknown"]}})) is error],
 ["initial delay holds first slide",story.sample(delayed,49).waiting and story.sample(delayed,49).progress==0 and story.sample(delayed,149).active_id=="a"],
 ["delay shifts completion",not story.sample(delayed,449).complete and story.sample(delayed,450).complete],
 ["fade transition sample",entering.transition.kind=="fade" and entering.transition.progress==0.5],
 ["live delay and frame",delayed_tick.state.active_id=="a" and delayed_tick.state.story_frame.transition.progress==0.5],
 ["seek skips initial delay",t.update(delayed_play.state,{type:"seek",time_ms:100},{spec:delayed_raw,now:1010}).state.active_id=="b"],
 ["frozen transition style",contains(format(frozen,'html'),"opacity:0.5")],
 ["progress indicators",contains(format(frozen,'html'),"lambda-timeline-overall-progress") and contains(format(frozen,'html'),"Current event progress")],
 ["reverse slide progress",abs(story.sample(reverse,20).progress-0.2)<0.000000000001 and abs(story.sample(reverse,20).overall_progress-0.05)<0.000000000001],
 ["reduced motion keeps timing",story.sample({*:delayed,motion:"reduced"},70).transition.progress==1 and story.sample({*:delayed,motion:"reduced"},70).active_id=="a"],
 ["slide and reveal styles",starts_with(story.style(t.validate({*:delayed_raw,slideshow:{duration:100,transition:"slide",transition_duration:40}}),
    {active_id:"a",playback:{elapsed:20,phase:"playing"}}),"transform:translateX") and
    starts_with(story.style(t.validate({*:delayed_raw,slideshow:{duration:100,transition:"reveal",transition_duration:40}}),
    {active_id:"a",playback:{elapsed:20,phase:"playing"}}),"clip-path:inset")],
 ["unknown easing rejected",story.plan(t.validate({*:raw,slideshow:{easing:"missing"}})) is error],
 ["invalid transition duration",t.validate({*:raw,slideshow:{transition_duration:-1}}) is error],
 ["manual navigation retains new playhead",manual.state.active_id=="b" and t.update(manual.state,{type:"tick"},{spec:raw,now:1100}).state.active_id=="b" and manual.state.playback.elapsed==100],
 ["outgoing slide identity",fade_sample.transition.outgoing_id=="a"],
 ["outgoing fade",story.style(fade_spec,{active_id:"b",story_frame:fade_sample,playback:{phase:"playing"}},true)=="opacity:0.5;"],
 ["outgoing card retired at transition end",story.sample(fade_plan,140).transition.outgoing_id==null],
 ["repeat enters from last slide",story.sample(fade_plan,420).transition.outgoing_id=="c"],
 ["alternate turn keeps same card",story.sample({*:fade_plan,direction:"alternate"},420).transition.outgoing_id==null],
 ["reverse exact boundary",story.sample(reverse,100).active_id=="b" and story.sample(reverse,100).transition.outgoing_id=="c"],
 ["reduced motion retires outgoing immediately",story.sample({*:fade_plan,motion:"reduced"},120).transition.outgoing_id==null],
 ["frozen outgoing card is inert",contains(fade_html,"data-timeline-outgoing=\"a\"") and contains(fade_html,"inert=\"\"") and contains(fade_html,"aria-hidden=\"true\"")],
 ["outgoing media uses shared poster snapshot",contains(media_html,"poster.png") and not contains(media_html,"<video")],
 ["completed story retires outgoing",story.sample(fade_plan,800).transition.outgoing_id==null]

]
