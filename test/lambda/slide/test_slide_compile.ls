import slide: lambda.slide

let deck = <presentation title: "Example", width: 640.0, height: 360.0,
    <slide id: "one", <group id: "g", width: 200.0, height: 100.0,
        <text id: "t", "Hello">>
        <cue id: "build", <sequence
            <effect target: "t", kind: 'fade-in', duration: 100.0, delay: 25.0>
            <parallel
                <effect target: "t", kind: 'spin', duration: 200.0>
                <effect target: "g", kind: 'pulse', duration: 300.0>
            >
        >>
    >
>
let plan = slide.compile(deck)^
fn message(deck) => slide.compile(deck) ^ { ^.message }
let checks = [
    plan.width == 640.0,
    len(plan.slides[0].targets) == 2,
    plan.slides[0].targets[1].dom_key == "s0-o0-0",
    plan.slides[0].cues[0].duration_ms == 425.0,
    contains(message(<presentation>), "no slides"),
    contains(message(<presentation width: 0.0, <slide>>), "invalid numeric width"),
    contains(message(<presentation width: inf, <slide>>), "invalid numeric width"),
    contains(message(<presentation nonsense: 1, <slide>>), "unsupported attribute nonsense"),
    contains(message(<presentation <slide <text id: "x"> <text id: "x">>>), "duplicate ID x"),
    contains(message(<presentation <slide <cue <effect target: "missing", kind: 'appear'>>>>), "unknown target missing"),
    contains(message(<presentation <slide <text id: "x"> <cue <parallel
        <effect target: "x", kind: 'fade-in'> <effect target: "x", kind: 'fade-out'>>>>>), "parallel writers"),
    contains(message(<presentation <slide <text id: "x"> <cue <effect target: "x", kind: 'appear'>>
        <cue start: 'entry', <effect target: "x", kind: 'appear'>>>>), "entry cues must precede"),
    contains(message(<presentation <slide <group <cue>>>>), "group accepts only slide objects")
]
checks
