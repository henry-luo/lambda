import slide: lambda.slide
import morph: lambda.slide.morph

let deck = <presentation width: 400.0, height: 300.0,
    <slide id: "first", <shape id: "a", morph_id: "dot", fill: "#ff0000", width: 40.0, height: 40.0>
        <cue <effect target: "a", kind: 'highlight', color: "#0000ff", duration: 100.0>>
        <cue <sequence
            <effect target: "a", kind: 'highlight', color: "#00ff00", duration: 100.0>
            <effect target: "a", kind: 'highlight', color: "#ff0000", duration: 100.0>>>>
    <slide id: "second", transition: 'morph', transition_duration: 100.0,
        <shape id: "b", morph_id: "dot", fill: "#0000ff", width: 40.0, height: 40.0>>>
let plan = slide.compile(deck)^
let ps = slide.reduce(plan, slide.initial_state(plan), {command: 'seek', address: {cue: 1, time_ms: 0.0}})^
let first = slide.sample(plan, ps)^[0]
let halfway = slide.sample(plan, {*: ps, time_ms: 50.0})^[0]
let later = slide.sample(plan, {*: ps, time_ms: 150.0})^[0]
let paint_deck = <presentation <slide <shape id: "s">
    <cue <effect target: "s", kind: 'keyframes', channel: 'paint', duration: 100.0,
        stops: [[0.0, "#ff0000"], [1.0, "#0000ff"]]>>>>
let color_plan = slide.compile(paint_deck)^
let initial = slide.sample(color_plan, slide.initial_state(color_plan))^[0]
let line_deck = <presentation <slide <shape id: "line", kind: 'line', stroke: "#0000ff">
    <cue <effect target: "line", kind: 'highlight', color: "#00ff00", duration: 100.0>>>>
let line_plan = slide.compile(line_deck)^;
[
    first.paint == "rgba(0,0,255,1)",
    halfway.paint == "rgba(0,127.5,127.5,1)",
    later.paint == "rgba(127.5,127.5,0,1)",
    initial.paint == "rgba(255,0,0,1)",
    initial.paint_rgba == [1.0, 0.0, 0.0, 1.0],
    plan.slides[1].morph.pairs[0].ac == [1.0, 0.0, 0.0, 1.0],
    morph.sample(plan, {*: ps, slide: 1, outgoing: 0, phase: 'transition', time_ms: 50.0}).incoming[0].visual.paint == "rgba(127.5,0,127.5,1)",
    slide.sample(line_plan, slide.initial_state(line_plan))^[0].paint == "#0000ff",
    contains(format(slide.snapshot(line_deck, {cue: 0, time_ms: 50.0})^, 'html'), "stroke=\"rgba(0,127.5,127.5,1)\"")
]
