// lambda.paint M1: libmypaint-model brushes, their dynamics and .myb import
// (Lambda_Pkg_Paint.md PT6; Radiant_Design_Paint_Engine.md PE4, PE5).
import paint: lambda.paint
fn check(label, value) => value

let blank = paint.layer(160, 64)^
let ink = [0.1, 0.3, 0.8]

// libmypaint's default brush: radius e^2, two dabs per actual radius, opacity scaled by pressure
let base = paint.brush({kind: 'mypaint'})^
check("default-brush", base.kind == 'mypaint' and base.compiled.base_radius > 7.38 and base.compiled.base_radius < 7.39)
let line = [[10, 32, 1.0], [150, 32, 1.0]]
let d = paint.dabs(base, ink, line)^
check("distance-spacing", d.count >= 36 and d.count <= 38 and len(d.table) == d.count * 34)
let painted = paint.stroke(blank, base, ink, line)^
check("full-pressure-paints", paint.pixel(painted.layer, 80, 32)^[3] == 255)
let feather = paint.stroke(blank, base, ink, [[10, 32, 0.0], [150, 32, 0.0]])^
check("zero-pressure-paints-nothing", paint.pixel(feather.layer, 80, 32)^[3] == 0)

// dabs_per_second paints while the pen rests: 0.5 s at 20 dabs per second
let timed = paint.brush({kind: 'mypaint', settings: {dabs_per_actual_radius: 0.0, dabs_per_second: 20.0, opaque_multiply: 1.0}})^
let still = paint.dabs(timed, ink, [[40, 32, 1.0, 0.0, 0.0, 0.0, 0.0], [40, 32, 1.0, 0.0, 0.0, 0.0, 0.5]])^
check("time-spacing", still.count >= 9 and still.count <= 10)

// a stroke split into batches paints exactly the whole stroke, keyed randomness included
let path = [[10, 20, 0.3], [40, 30, 0.9], [70, 26, 0.6], [100, 40, 1.0], [140, 30, 0.5]]
let lively = paint.brush({kind: 'mypaint', settings: {
    radius_logarithmic: {base: 1.2, inputs: {pressure: [[0.0, -0.5], [1.0, 0.5]]}},
    offset_by_random: 0.3, radius_by_random: 0.2, slow_tracking: 1.0}})^
let first = paint.stroke_step(blank, lively, ink, [path[0], path[1], path[2]])^
let second = paint.stroke_step(blank, lively, ink, [path[3], path[4]], first)^
let whole = paint.stroke(blank, lively, ink, path)^
check("split-equals-whole", second.layer == whole.layer)
check("keyed-randomness-repeats", paint.stroke(blank, lively, ink, path)^.layer == whole.layer)
check("pressure-sizes-dabs", paint.dabs(lively, ink, [[10, 20, 1.0], [140, 20, 1.0]])^.count <
    paint.dabs(lively, ink, [[10, 20, 0.1], [140, 20, 0.1]])^.count)

// smudge carries the canvas colour along the stroke; its buckets travel with the continuation
let red = paint.stroke(blank, paint.brush({size: 40.0, hardness: 1.0})^, [1.0, 0.0, 0.0], [[60, 32], [100, 32]])^.layer
let smear = paint.brush({kind: 'mypaint', settings: {smudge: 1.0, smudge_length: 0.0, opaque_multiply: 1.0}})^
let smeared = paint.stroke_step(red, smear, [0.0, 0.0, 1.0], [[70, 32, 1.0], [90, 32, 1.0]])^
let px = paint.pixel(smeared.layer, 80, 32)^
check("smudge-carries-canvas", px[0] > 200 and px[2] < 50 and smeared.smudge != null)
let pure = paint.stroke_step(red, paint.brush({kind: 'mypaint', settings: {opaque_multiply: 1.0}})^, [0.0, 0.0, 1.0],
    [[70, 32, 1.0], [90, 32, 1.0]])^
check("no-smudge-paints-brush-colour", paint.pixel(pure.layer, 80, 32)^[2] > 200 and pure.smudge == null)

// .myb import: rendered settings become the brush, changed unrendered ones are reported
let myb = {version: 3, comment: "", settings: {
    opaque: {base_value: 0.9, inputs: {}},
    opaque_multiply: {base_value: 0.0, inputs: {pressure: [[0.0, 0.0], [1.0, 1.0]]}},
    radius_logarithmic: {base_value: 1.5, inputs: {}},
    color_h: {base_value: 0.3, inputs: {}},
    snap_to_pixel: {base_value: 1.0, inputs: {}},
    softness: {base_value: 0.0, inputs: {pressure: []}}}}
let imported = paint.brush_from_myb(myb)^
check("myb-import", imported.brush.kind == 'mypaint' and imported.ignored == ["snap_to_pixel"] and
    imported.brush.compiled.base_radius > 4.48 and imported.brush.compiled.base_radius < 4.49)
check("myb-paints", paint.pixel(paint.stroke(blank, imported.brush, ink, line)^.layer, 80, 32)^[3] > 200)

check("unrendered-setting", match (paint.brush({kind: 'mypaint', settings: {snap_to_pixel: 1.0}}) ^ { ^ }) { case error: true default: false })
check("descending-curve", match (paint.brush({kind: 'mypaint', settings: {opaque: {base: 1.0,
    inputs: {pressure: [[1.0, 0.0], [0.0, 1.0]]}}}}) ^ { ^ }) { case error: true default: false })
check("unknown-input", match (paint.brush({kind: 'mypaint', settings: {opaque: {base: 1.0,
    inputs: {altitude: [[0.0, 0.0], [1.0, 1.0]]}}}}) ^ { ^ }) { case error: true default: false })
check("tilt-range", match (paint.dabs(base, ink, [[1, 1, 0.5, 120.0]]) ^ { ^ }) { case error: true default: false })
check("myb-version", match (paint.brush_from_myb({version: 2, settings: {}}) ^ { ^ }) { case error: true default: false })
