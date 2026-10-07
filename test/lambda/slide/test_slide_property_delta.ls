import html: lambda.slide.html
import samples: lambda.slide.sample

let base = samples.baseline({id: "target", dom_key: "s0-o0", opacity: 1.0, paint: "red"})
let changes = [
    {*: base, opacity: 0.5}, {*: base, opacity: 0.0}, {*: base, visible: 0.0},
    {*: base, tx: 10.0}, {*: base, ty: -5.0}, {*: base, rotation: 45.0},
    {*: base, sx: 0.5}, {*: base, sy: 2.0},
    {*: base, clip: 0.25, clip_direction: 'left'},
    {*: base, clip_direction: 'right'}, {*: base, paint: "blue"}
]
let clipped = changes[8]
let hidden = changes[1];
[
    // applying a delta preserves the same CSS as a fresh static projection.
    all([for (v in changes) {*: html.effect_properties(base), *: html.effect_properties(v, base)} == html.effect_properties(v)]),
    html.effect_properties(base, base) == {},
    html.effect_properties(changes[0], base) == {opacity: "0.5"},
    html.effect_properties(hidden, base) == {opacity: "0", visibility: "hidden", ["pointer-events"]: "none"},
    html.effect_properties(base, hidden) == {opacity: "1", visibility: "visible", ["pointer-events"]: "auto"},
    html.effect_properties({*: hidden, visible: 0.0}, hidden) == {},
    html.effect_properties(clipped, base) == {["clip-path"]: "inset(0 0 0 25%)"},
    html.effect_properties(base, clipped) == {["clip-path"]: "none"},
    html.effect_properties({*: clipped, clip_direction: 'top'}, clipped) == {["clip-path"]: "inset(25% 0 0 0)"},
    html.effect_properties(changes[9], base) == {},
    html.effect_properties(changes[10], base) == {}
]
