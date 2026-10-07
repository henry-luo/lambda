import slide: lambda.slide
import morph: lambda.slide.morph
import transitions: lambda.slide.transitions

let deck = <presentation width: 400.0, height: 300.0,
    <slide id: "a", background: "#ffffff", <shape id: "a", morph_id: "dot", fill: "#ff0000",
        x: 10.0, y: 20.0, width: 40.0, height: 40.0>
        <text id: "old", morph_id: "title", "Before">>
    <slide id: "b", transition: 'morph', transition_duration: 100.0, background: "#000000",
        <shape id: "b", morph_id: "dot", fill: "#0000ff", x: 110.0, y: 120.0, width: 80.0, height: 80.0>
        <text id: "new", morph_id: "title", "After">>
>
let plan = slide.compile(deck)^
let ps = {*: slide.initial_state(plan), slide: 1, outgoing: 0, phase: 'transition', time_ms: 50.0}
let sampled = morph.sample(plan, ps)
let changed = sampled.incoming[1]
fn message(deck) => slide.compile(deck) ^ { ^.message };
[
    len(plan.slides[1].morph.pairs) == 2,
    plan.slides[1].morph.pairs[0].compatible,
    not plan.slides[1].morph.pairs[1].compatible,
    abs(sampled.incoming[0].geometry.x - 60.0) < 0.0001,
    abs(sampled.incoming[0].geometry.width - 60.0) < 0.0001,
    sampled.outgoing[0].visual.opacity == 0.0,
    abs(changed.visual.opacity - 0.5) < 0.0001,
    sampled.incoming[0].visual.paint == "rgba(127.5,0,127.5,1)",
    sampled.background == "rgba(127.5,127.5,127.5,1)",
    contains(message(<presentation <slide <shape morph_id: "x">>
        <slide transition: 'morph', <shape id: "b", morph_id: "x">>>), "explicit object IDs"),
    transitions.sample(plan, {*: ps, phase: 'waiting'}).incoming.opacity == 1.0
]
