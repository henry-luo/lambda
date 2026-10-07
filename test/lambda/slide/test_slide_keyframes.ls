import slide: lambda.slide
let deck = <presentation <slide <text id: "t", "Keyframes">
    <cue <sequence <effect target: "t", kind: 'appear'>
        <effect target: "t", kind: 'keyframes', channel: 'tx', duration: 100.0,
            stops: [[0.0, 0.0], [0.25, 100.0], [1.0, 0.0]]>>>>>
let plan = slide.compile(deck)^
let ps = slide.reduce(plan, slide.initial_state(plan), {command: "seek", address: {cue: 0, time_ms: 25.0}})^
fn message(deck) => slide.compile(deck) ^ { ^.message };
[
    slide.sample(plan, ps)^[0].tx == 100.0,
    slide.sample(plan, {*: ps, time_ms: 100.0})^[0].tx == 0.0,
    contains(message(<presentation <slide <text id: "t"> <cue <parallel
        <effect target: "t", kind: 'appear'> <effect target: "t", kind: 'disappear'>>>>>), "parallel writers"),
    contains(message(<presentation <slide <text id: "t"> <cue <effect target: "t", kind: 'keyframes', channel: 'tx',
        stops: [[0.0, 0.0], [0.0, 20.0], [1.0, 40.0]]>>>>), "fractions must increase"),
    contains(message(<presentation <slide layout: 'unknown'>>), "unsupported layout"),
    contains(slide.compile(deck, {width: 0.0}) ^ { ^.message }, "numeric width"),
    contains(slide.compile(deck, {instance: "bad id"}) ^ { ^.message }, "instance ID")
]
