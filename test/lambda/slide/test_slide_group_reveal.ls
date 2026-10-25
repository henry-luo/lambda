import slide: lambda.slide

// a group hidden by visibility alone (appear, wipe-in) hides its nested objects:
// they inherit visibility rather than forcing themselves visible.
let deck = <presentation title: "Group reveal", width: 640.0, height: 360.0,
    <slide id: "one",
        <group id: "appear-group", x: 20.0, y: 20.0, width: 300.0, height: 60.0,
            <text id: "appear-text", x: 0.0, y: 0.0, width: 300.0, height: 60.0, "Appear">>
        <group id: "wipe-group", x: 20.0, y: 100.0, width: 300.0, height: 60.0,
            <text id: "wipe-text", x: 0.0, y: 0.0, width: 300.0, height: 60.0, "Wipe">>
        <cue <effect target: "appear-group", kind: 'appear'>>
        <cue <effect target: "wipe-group", kind: 'wipe-in', from: 'left', duration: 100.0>>
    >
>
let initial = format(slide.snapshot(deck)^, 'html')
let final = format(slide.snapshot(deck, {cue: 'final'})^, 'html');
[
    contains(initial, "data-slide-object=\"appear-group\" aria-hidden=\"true\""),
    contains(initial, "data-slide-object=\"wipe-group\" aria-hidden=\"true\""),
    not contains(initial, "visibility:visible"),
    contains(initial, "visibility:inherit"),
    not contains(final, "visibility:hidden")
]
