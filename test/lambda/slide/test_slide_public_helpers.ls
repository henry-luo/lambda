import slide: lambda.slide
import html: lambda.slide.html

let builds = slide.paragraphs("points", ["First", <p "Second">], {width: 300.0, line_height: 70.0, duration: 100.0})^
let deck = <presentation id: "helpers", width: 400.0, height: 300.0,
    <slide id: "start", *[*builds, <notes "Presenter only">]>>
let plan = slide.compile(deck)^
let initial = slide.initial_state(plan)
let halfway = slide.reduce(plan, initial, {command: 'seek', address: {cue: 0, time_ms: 50.0}})^
let frames = slide.sample(plan, halfway)^
let status = slide.diagnostic(plan, {*: halfway, phase: 'cue'}, 17)
let handout = format(slide.handout(deck)^, 'html')
let fit = html.fit(plan, 800.0, 800.0);
[
    len(builds) == 3 and len(plan.slides[0].cues) == 2,
    plan.slides[0].targets[2].y == 70.0,
    frames[1].opacity == 0.5 and frames[2].opacity == 0.0,
    status.deck == "helpers" and status.cue == "points-cue0" and status.frame_token == 17,
    len(status.active_tracks) == 2,
    contains(handout, "Presenter only") and contains(handout, "slide-speaker-notes"),
    not contains(format(slide.snapshot(deck)^, 'html'), "Presenter only"),
    contains(format(slide.snapshot(deck)^, 'html'), "inert=\"\""),
    fit.scale == 2.0 and fit.x == 0.0 and fit.y == 100.0,
    contains(slide.compile(deck, {["typo"]: true}) ^ { ^.message }, "unsupported attribute typo"),
    contains(slide.snapshot(deck, {unknown: 1}) ^ { ^.message }, "unsupported attribute unknown"),
    contains(slide.paragraphs("points", [], {}) ^ { ^.message }, "nonempty content array"),
    not slide.needs_frame(initial) and slide.needs_frame({*: initial, phase: 'cue'}),
    not slide.needs_frame({*: initial, phase: 'cue', paused: true})
]
