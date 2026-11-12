// lambda.paint bristle brushes (Lambda_Pkg_Paint.md PT5, PE10): per-hair trails with lag,
// depletion and pickup, over rgba and pigment layers.
import paint: lambda.paint
fn check(label, value) => value
fn column(l, x) => sum([for (y in 16 to 47) paint.pixel(l, x, y)[3]]) ^ { -1 }
fn alpha(l, x, y) => paint.pixel(l, x, y)[3] ^ { -1 }
fn blue_over_red(l, x, y) => (let p = paint.pixel(l, x, y) ^ { [0, 0, 0, 0] }, p[2] > p[0])

let blank = paint.layer(320, 64)^
let stroke = [for (i in 0 to 30) [10.0 + 10.0 * i, 32.0, 1.0]]
let ink = [0.1, 0.2, 0.8]
let br = paint.brush({kind: 'bristle', size: 24.0, count: 12, depletion: 0.006})^
check("bristle-brush", br.kind == 'bristle' and br.count == 12 and abs(br.width - 3.2) < 0.000001)

// the continuation carries each bristle's state: offset, stiffness, width, load, tip and ink
let whole = paint.stroke_step(blank, br, ink, stroke)^
check("state-layout", shape(whole.bristles) == [12, 11])
check("loads-fall", all([for (j in 0 to 11) whole.bristles[j][4] < 1.0 * 1.3]) and sum([for (j in 0 to 11) whole.bristles[j][4]]) < 6.0)

// depletion is visible along a long stroke: the trail is fainter at its end
check("depletion-visible", column(whole.layer, 30) > 2.5 * column(whole.layer, 290))
let rich = paint.stroke_step(blank, paint.brush({kind: 'bristle', size: 24.0, count: 12, depletion: 0.0})^, ink, stroke)^
check("no-depletion-even", abs(column(rich.layer, 30) - column(rich.layer, 290)) < 0.1 * column(rich.layer, 30))

// bristles leave gaps between hairs that a round dab would fill
check("hairs-leave-gaps", len([for (y in 18 to 46 where (alpha(whole.layer, 60, y) == 0) ^ { false }) y]) > 0)

// a stroke split into batches paints exactly the whole stroke
let first = paint.stroke_step(blank, br, ink, [for (i in 0 to 12) stroke[i]])^
let split = paint.stroke_step(blank, br, ink, [for (i in 13 to 30) stroke[i]], first)^
check("split-equals-whole", paint.present(split.layer)^ == paint.present(whole.layer)^ and split.bristles == whole.bristles)

// placement is keyed by the brush seed: equal seeds paint equal trails, other seeds differ
let again = paint.stroke_step(blank, br, ink, stroke)^
let reseeded = paint.stroke_step(blank, paint.brush({kind: 'bristle', size: 24.0, count: 12, depletion: 0.006, seed: 7})^, ink, stroke)^
check("seeded", again.bristles == whole.bristles and reseeded.bristles != whole.bristles)

// pickup: a bristle crossing paint takes some of it into its ink
let red = paint.stroke_step(blank, paint.brush({size: 60.0, hardness: 1.0})^, [0.9, 0.1, 0.1], [[200.0, 32.0]])^.layer
let picky = paint.stroke_step(red, paint.brush({kind: 'bristle', size: 24.0, count: 4, pickup: 0.3})^, ink, stroke)^
check("pickup-mixes", picky.bristles[1][8] > 0.2)

// on a pigment layer the bristles carry paint per unit coverage and deposit film
let canvas = paint.layer(320, 64, {format: 'pigment'})^
let oil = paint.brush({kind: 'bristle', size: 24.0, count: 12, depletion: 0.002, amount: 1.5})^
let painted = paint.stroke_step(canvas, oil, [0.1, 0.4, 0.8], stroke)^
check("pigment-state", shape(painted.bristles) == [12, 15] and painted.bristles[0][14] == 1.5)
check("pigment-trails", blue_over_red(painted.layer, 60, 32))

// adjusting a bristle brush keeps its kind and rescales the hairs
let wide = paint.adjust_brush(br, {size: 48.0})^
check("adjust", wide.kind == 'bristle' and wide.size == 48.0 and abs(wide.width - 6.4) < 0.000001 and wide.count == 12)

// refused: bad counts, a tip, wash mode
check("count-refused", match (paint.brush({kind: 'bristle', count: 0}) ^ { ^ }) { case error: true default: false })
check("tip-refused", match (paint.brush({kind: 'bristle', tip: paint.tip('flat')^}) ^ { ^ }) { case error: true default: false })
check("wash-refused", match (paint.brush({kind: 'bristle', mode: 'wash'}) ^ { ^ }) { case error: true default: false })
