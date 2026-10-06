import slide: lambda.slide
import easing: lambda.slide.easing
import sampler: lambda.slide.sample

let deck = <presentation <slide id: "s", <text id: "t", "Hello">
    <cue id: "in", <effect target: "t", kind: 'fly-in', from: 'left', distance: 80.0, duration: 100.0, delay: 20.0>>
    <cue id: "emphasis", <effect target: "t", kind: 'pulse', scale: 1.5, duration: 100.0>>
    <cue id: "out", <effect target: "t", kind: 'fade-out', duration: 100.0>>
>>
let plan = slide.compile(deck)^
let scene = plan.slides[0]
fn sample(cue, ms) => sampler.scene(scene, cue, ms)[0]
let initial = sample(-1, 0.0)
let delayed = sample(0, 10.0)
let middle = sample(0, 70.0)
let completed = sample(0, 120.0)
let pulse_mid = sample(1, 50.0)
let pulse_end = sample(1, 100.0)
let exit_mid = sample(2, 50.0)
let exit_end = sample(2, 100.0);
[
    initial.visible == 0.0 and initial.tx == -80.0,
    delayed.visible == 0.0,
    middle.visible == 1.0 and middle.tx == -40.0 and middle.opacity == 0.5,
    completed.opacity == 1.0 and completed.tx == 0.0,
    pulse_mid.sx == 1.5 and pulse_mid.sy == 1.5,
    pulse_end.sx == 1.0 and pulse_end.sy == 1.0,
    exit_mid.visible == 1.0 and exit_mid.opacity == 0.5,
    exit_end.visible == 0.0 and exit_end.opacity == 0.0,
    easing.sample('ease-out-cubic', 0.5) == 0.875,
    abs(easing.sample([0.0, 0.0, 1.0, 1.0], 0.25) - 0.25) < 0.0000001,
    easing.sample('ease', 0.0) == 0.0 and easing.sample('ease', 1.0) == 1.0,
    sampler.address(plan, {cue: 'final'})^.time_ms == 100.0
]
