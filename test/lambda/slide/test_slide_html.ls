import slide: lambda.slide

let deck = <presentation title: "Slide & sample", width: 640.0, height: 360.0,
    <slide id: "one", <text id: "title", x: 20.0, y: 30.0, width: 200.0, height: 80.0, "Hello <slides>">
        <shape id: "dot", kind: 'circle', width: 40.0, height: 40.0, fill: "red">
        <cue <effect target: "title", kind: 'fade-in', duration: 100.0>>>
>
let initial = format(slide.snapshot(deck)^, 'html')
let middle = format(slide.snapshot(deck, {cue: 0, time_ms: 50.0})^, 'html')
let live = format(slide.page(deck, {instance: "demo"})^, 'html');
[
    contains(initial, "<title>Slide"),
    contains(initial, "Hello &lt;slides&gt;"),
    contains(initial, "<ellipse"),
    contains(initial, "visibility:hidden"),
    contains(middle, "opacity:0.5"),
    contains(live, "id=\"demo\""),
    contains(live, "data-slide-command=\"play\""),
    contains(live, "id=\"demo-s0-o0\""),
    not contains(live, "<script")
]
