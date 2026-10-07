import slide: lambda.slide

let deck = <presentation
    <slide id: "a", <text id: "t", "A">
        <cue start: 'entry', <effect target: "t", kind: 'fade-in', duration: 100.0>>
        <cue start: 'entry', <effect target: "t", kind: 'spin', duration: 100.0>>
        <cue <effect target: "t", kind: 'pulse', duration: 100.0>>
    >
    <slide id: "b", transition: 'fade', transition_duration: 200.0, <text id: "u", "B">
        <cue start: 'entry', <effect target: "u", kind: 'fade-in', duration: 100.0>>
    >
>
let plan = slide.compile(deck)^
fn command(ps, cmd, ms) => slide.reduce(plan, ps, {command: cmd, time_ms: ms})^
let initial = slide.initial_state(plan)
let running = command(initial, 'play', 0.0)
let late = command(running, 'frame', 150.0)
let settled = command(late, 'frame', 250.0)
let clicked = command(settled, 'next', 250.0)
let paused = command(clicked, 'pause', 280.0)
let still = command(paused, 'frame', 999.0)
let resumed = command(still, 'resume', 1000.0)
let resumed_tick = command(resumed, 'frame', 1040.0)
let skipped = command(resumed_tick, 'next', 1040.0)
let transition = command(skipped, 'next', 1040.0)
let late_transition = command(transition, 'frame', 1290.0)
let back = command(late_transition, 'previous', 1290.0)
let reduced = command(slide.initial_state(plan, {reduced_motion: true}), 'play', 0.0);
[
    running.phase == 'cue' and running.cue == 0,
    late.cue == 1 and late.time_ms == 50.0,
    settled.phase == 'waiting' and settled.cue == 1,
    clicked.cue == 2 and clicked.phase == 'cue',
    paused.time_ms == 30.0 and still.time_ms == 30.0,
    resumed_tick.time_ms == 70.0,
    skipped.phase == 'waiting' and skipped.time_ms == 100.0,
    transition.phase == 'transition' and transition.slide == 1 and transition.outgoing == 0,
    late_transition.phase == 'cue' and late_transition.time_ms == 50.0,
    back.slide == 1 and back.cue == -1,
    reduced.phase == 'waiting' and reduced.cue == 1,
    slide.reduce(plan, initial, {command: 'seek', address: {slide: "b", cue: 'final'}})^.time_ms == 100.0
]
