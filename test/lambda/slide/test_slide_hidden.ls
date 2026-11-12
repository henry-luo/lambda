import slide: lambda.slide

let deck=<presentation
    <slide id:"hidden-first",hidden:true,<text "Hidden first">>
    <slide id:"one",<text "One">>
    <slide id:"hidden-middle",hidden:true,<text "Hidden middle">>
    <slide id:"two",<text "Two">>
    <slide id:"hidden-last",hidden:true,<text "Hidden last">>>
let plan=slide.compile(deck,{autoplay_dwell_ms:1000.0})^
let start=slide.initial_state(plan)
fn command(ps,cmd) any^ => slide.reduce(plan,ps,{command:cmd,time_ms:0.0})^
let next=command(start,'next')^
let previous=command(next,'previous')^
let jump=slide.reduce(plan,start,{command:'jump',slide:2,time_ms:0.0})^
let autoplay=slide.reduce(plan,command(start,'play')^,{command:'frame',time_ms:3100.0})^
let timeline=slide.timeline(deck,{autoplay_dwell_ms:1000.0})^
let sought=slide.reduce(plan,start,{command:'seek_time',position_ms:1000.0,time_ms:0.0})^
let all_hidden=slide.compile(<presentation <slide id:"only",hidden:true,<text "Still addressable">>>)^;
[
    start.slide==1,
    next.slide==3,
    previous.slide==1,
    command(start,'previous')^.slide==1,
    command(next,'next')^.slide==3,
    command(start,'end')^.slide==3,
    command(next,'restart')^.slide==1,
    jump.slide==2 and command(jump,'next')^.slide==3 and command(jump,'previous')^.slide==1,
    autoplay.slide==3,
    slide.initial_state(plan,{start_slide:2}).slide==2,
    slide.initial_state(all_hidden).slide==0,
    slide.reduce(all_hidden,slide.initial_state(all_hidden),{command:'next',time_ms:0.0})^.slide==0,
    timeline.duration_ms==1000.0 and timeline.slides[0].start_ms==timeline.slides[0].end_ms,
    timeline.slides[2].start_ms==timeline.slides[2].end_ms and timeline.slides[4].start_ms==timeline.slides[4].end_ms,
    sought.slide==3 and slide.position(plan,jump)==timeline.slides[2].start_ms
]
