import slide: lambda.slide

let deck = <presentation
    <slide id: "a", <text id: "title", "A"> <text id: "detail", "Detail">
        <cue start: 'entry', <effect target: "title", kind: 'fade-in', duration: 100.0>>
        <cue <effect target: "detail", kind: 'fade-in', duration: 100.0>>>
    <slide id: "b", transition: 'fade', transition_duration: 200.0, <text id: "title", "B">
        <cue start: 'entry', <effect target: "title", kind: 'fade-in', duration: 100.0>>>
    <slide id: "c", <text "C">>>
let plan = slide.compile(deck, {autoplay_dwell_ms: 500.0})^
fn control(ps, action, at_ms) => slide.reduce(plan, ps, {command: action, time_ms: at_ms})^
let initial = slide.initial_state(plan)
let opening = control(initial, 'activate', 0.0)
let parked = control(opening, 'frame', 100.0)
let playing = control(parked, 'play', 100.0)
let waiting = control(playing, 'frame', 300.0)
let paused = control(waiting, 'pause', 350.0)
let resumed = control(paused, 'play', 1000.0)
let before_build = control(resumed, 'frame', 1249.0)
let build = control(before_build, 'frame', 1250.0)
let built = control(build, 'frame', 1350.0)
let transition = control(built, 'frame', 1850.0)
let paused_transition = control(transition, 'pause', 1900.0)
let resumed_transition = control(paused_transition, 'play', 2000.0)
let halfway_transition = control(resumed_transition, 'frame', 2050.0)
let destination = control(transition, 'frame', 2050.0)
let arrived = control(destination, 'frame', 2150.0)
let ended = control(arrived, 'frame', 2650.0)
let replay = control(ended, 'play', 2700.0)
let restarted = control(ended, 'restart', 2700.0)
let jumped = slide.reduce(plan, initial, {command: 'jump', slide: "b", time_ms: 0.0})^
let slide_replay = control(jumped, 'restart-slide', 10.0)
let running = control(initial, 'play', 0.0)
let repeated = control(running, 'play', 50.0)
let paused_cue = control(running, 'pause', 40.0)
let continued_cue = control(control(paused_cue, 'play', 1000.0), 'frame', 1030.0)
let late = control(running, 'frame', 2000.0)
let reduced = control(slide.initial_state(plan, {reduced_motion: true}), 'play', 0.0)
let reduced_end = control(reduced, 'frame', 1500.0)
fn speed(ps, rate, at_ms) => slide.reduce(plan, ps, {command: 'speed', rate: rate, time_ms: at_ms})^
let faster = speed(running, 2.0, 25.0)
let fastest = speed(running, 4.0, 25.0)
let fastest_end = control(fastest, 'frame', 618.75)
let fast_entry = control(faster, 'frame', 62.5)
let slower = speed(fast_entry, 0.5, 187.5)
let slow_build = control(slower, 'frame', 687.5)
let slow_late = control(slower, 'frame', 2000.0)
let paused_speed = speed(paused_cue, 0.5, 1000.0)
let resumed_speed = control(control(paused_speed, 'resume', 2000.0), 'frame', 2040.0)
let fast_transition = speed(transition, 2.0, 1900.0)
let parked_speed = speed(parked, 2.0, 500.0);
[
    not parked.autoplay and not slide.needs_frame(parked),
    playing.autoplay and slide.needs_frame(playing),
    waiting.time_ms == 100.0 and waiting.wait_ms == 200.0 and slide.sample(plan, waiting)^[0].opacity == 1.0,
    paused.wait_ms == 250.0 and not slide.needs_frame(paused),
    resumed.autoplay and not resumed.paused and resumed.wait_ms == 250.0,
    before_build.cue == 0 and build.cue == 1 and build.phase == 'cue',
    built.time_ms == 100.0 and built.phase == 'waiting' and built.autoplay,
    transition.slide == 1 and transition.phase == 'transition',
    halfway_transition.time_ms == 100.0 and not halfway_transition.paused,
    destination.phase == 'cue' and destination.cue == 0 and destination.time_ms == 0.0,
    ended.slide == 2 and not ended.autoplay and not slide.needs_frame(ended),
    replay.slide == 0 and replay.autoplay and replay.phase == 'cue',
    restarted.slide == 0 and restarted.cue == 0 and not restarted.autoplay,
    slide_replay.slide == 1 and slide_replay.phase == 'cue' and not slide_replay.autoplay,
    repeated.anchor_ms == running.anchor_ms and repeated.generation == running.generation,
    continued_cue.time_ms == 70.0 and continued_cue.autoplay,
    late.slide == 2 and not slide.needs_frame(late),
    reduced_end.slide == 2 and not slide.needs_frame(reduced_end),
    control(control(initial, 'pause', 0.0), 'play', 50.0).phase == 'cue',
    slide.initial_state(slide.compile(deck)^).autoplay_dwell_ms == 3000.0,
    not control(playing, 'next', 200.0).autoplay and not control(playing, 'previous', 200.0).autoplay,
    contains(slide.compile(deck, {autoplay_dwell_ms: 0.0}) ^ { ^.message }, "autoplay_dwell_ms"),
    contains(control({*: initial, autoplay_dwell_ms: -1.0}, 'play', 0.0) ^ { ^.message }, "positive and finite"),
    faster.time_ms == 25.0 and control(faster, 'frame', 50.0).time_ms == 75.0,
    fast_entry.phase == 'waiting' and fast_entry.time_ms == 100.0 and fast_entry.wait_ms == 0.0,
    slower.wait_ms == 250.0 and slower.time_ms == 100.0,
    slow_build.cue == 1 and slow_build.phase == 'cue' and slow_build.time_ms == 0.0,
    slow_late.slide == 1 and slow_late.phase == 'transition' and slow_late.time_ms == 56.25,
    paused_speed.paused and paused_speed.time_ms == 40.0 and not slide.needs_frame(paused_speed),
    resumed_speed.time_ms == 60.0 and resumed_speed.playback_rate == 0.5,
    fast_transition.time_ms == 50.0 and control(fast_transition, 'frame', 1925.0).time_ms == 100.0,
    control(fast_transition, 'frame', 1975.0).phase == 'cue',
    parked_speed.time_ms == 100.0 and not slide.needs_frame(parked_speed),
    control(faster, 'restart', 50.0).playback_rate == 2.0,
    contains(speed(running, 0.0, 50.0) ^ { ^.message }, "positive and finite"),
    contains(speed(running, inf, 50.0) ^ { ^.message }, "positive and finite"),
    control(fastest, 'frame', 43.75).time_ms == 100.0,
    fastest_end.slide == 2 and not slide.needs_frame(fastest_end)
]
