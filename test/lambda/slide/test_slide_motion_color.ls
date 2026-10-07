import slide: lambda.slide
import motion: lambda.slide.motion
import color: lambda.slide.color

let route = motion.compile([[0.0, 0.0, 100.0, 0.0], [100.0, 0.0, 100.0, 100.0]], "test")^
let quarter = motion.sample(route, 0.25)
let turn = motion.sample(route, 0.75)
let cubic = motion.compile([[0.0, 0.0, 0.0, 100.0, 100.0, 100.0, 100.0, 0.0]], "test")^
let middle = motion.sample(cubic, 0.5)
let rgba = color.sample(color.parse("#ff000000", "test")^, color.parse("#0000ffff", "test")^, 0.5)
let deck = <presentation <slide <shape id: "dot", fill: "#ff0000", width: 50.0, height: 50.0>
    <cue <parallel <effect target: "dot", kind: 'motion', path: [[0.0, 0.0, 100.0, 0.0]], orient: 'auto', duration: 100.0>
        <effect target: "dot", kind: 'highlight', color: "#0000ff", duration: 100.0>>>>
>
let plan = slide.compile(deck)^
let ps = slide.reduce(plan, slide.initial_state(plan), {command: 'seek', address: {cue: 0, time_ms: 50.0}})^
let sampled = slide.sample(plan, ps)^;
[
    route.length == 200.0,
    quarter.x == 50.0 and quarter.y == 0.0,
    turn.x == 100.0 and turn.y == 50.0 and turn.angle == 90.0,
    abs(middle.x - 50.0) < 0.001 and abs(middle.y - 75.0) < 0.001,
    motion.sample(cubic, 1.0).x == 100.0,
    rgba[0] == 0.0 and rgba[2] == 1.0 and rgba[3] == 0.5,
    color.css(color.parse("#fff", "test")^) == "rgba(255,255,255,1)",
    sampled[0].tx == 50.0 and sampled[0].rotation == 0.0,
    sampled[0].paint == "rgba(127.5,0,127.5,1)"
]
