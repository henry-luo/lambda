// lambda.paint surfaces (Lambda_Pkg_Paint.md PT5, PE4): procedural paper and tips, bitmap-tip
// footprints, paper grain, and the session's paper texture.
import paint: lambda.paint
fn check(label, value) => value
fn span(p) => [min(flatten(p)), max(flatten(p))]
fn alpha(l, x, y) => paint.pixel(l, x, y)[3] ^ { -1 }

// papers are tileable height fields in [0, 1]; equal specs give equal planes
let cold = paint.paper('cold_press', {size: 64, seed: 3})^
let papers = [for (k in ['cold_press', 'rough', 'canvas']) paint.paper(k, {size: 64})^]
check("paper-shapes", all([for (p in papers) shape(p) == [64, 64] and span(p) == [0.0, 1.0]]))
check("smooth-is-flat", paint.paper('smooth')^ == as_float([[1.0]]))
check("paper-keyed", paint.paper('cold_press', {size: 64, seed: 3})^ == cold and paint.paper('cold_press', {size: 64, seed: 4})^ != cold)

// tips are coverage planes; a flat tip is a wide bar, empty above and below its band
let flat = paint.tip('flat', {size: 32})^
let chalk = paint.tip('chalk', {size: 32})^
let speck = paint.tip('speckle', {size: 32})^
check("tip-shapes", shape(flat) == [32, 32] and shape(chalk) == [32, 32] and shape(speck) == [32, 32])
check("flat-bar", flat[16][16] == 1.0 and flat[2][16] == 0.0 and flat[16][1] < flat[16][16])
check("chalk-grainy", chalk[16][16] >= 0.0 and len([for (v in flatten(chalk) where v > 0.0 and v < 1.0) v]) > 20)

// a flat-tipped brush paints a band where a round brush of the same size paints a disc
let blank = paint.layer(120, 60)^
let ink = [0.1, 0.2, 0.6]
let round = paint.brush({size: 30.0, hardness: 1.0})^
let nib = paint.brush({size: 30.0, hardness: 1.0, tip: flat})^
let line = [[60.0, 30.0, 1.0]]
let r = paint.stroke_step(blank, round, ink, line)^.layer
let n = paint.stroke_step(blank, nib, ink, line)^.layer
check("tip-footprint", alpha(r, 60, 22) == 255 and alpha(n, 60, 22) == 0 and alpha(n, 60, 30) == 255 and alpha(n, 72, 30) == 255)

// grain lets paper valleys show through; without paper the same brush paints solidly
let gritty = paint.brush({size: 30.0, hardness: 1.0, grain: 1.0})^
let path = [[20.0, 30.0, 1.0], [100.0, 30.0, 1.0]]
let plain = paint.stroke_step(blank, gritty, ink, path)^.layer
let grained = paint.stroke_step(blank, gritty, ink, path, null, {paper: cold})^.layer
fn thin(l) => len([for (x in 30 to 90, y in 24 to 36 where alpha(l, x, y) < 250) 1])
check("grain-shows-paper", thin(plain) == 0 and thin(grained) > 50)
check("no-paper-no-grain", plain == paint.stroke_step(blank, round, ink, path)^.layer)
let mp = paint.brush({kind: 'mypaint', settings: {radius_logarithmic: 2.5, opaque_multiply: 1.0}, grain: 1.0})^
check("mypaint-grain", thin(paint.stroke_step(blank, mp, ink, path, null, {paper: cold})^.layer) > 50)
// a mypaint brush with the flat tip paints its band, not a disc. libmypaint turns dabs 90 degrees by
// default (elliptical_dab_angle), so the band is laid flat; dabs follow its spacing, so use a real path.
let mp_nib = paint.adjust_brush(paint.brush({kind: 'mypaint', tip: flat, settings: {opaque_multiply: 1.0,
    elliptical_dab_angle: 0.0}})^, {size: 30.0})^
let nibbed = paint.stroke_step(blank, mp_nib, ink, [[40.0, 30.0, 1.0], [80.0, 30.0, 1.0]])^.layer
check("mypaint-tip", alpha(nibbed, 60, 30) > 200 and alpha(nibbed, 60, 22) == 0)

// angle jitter turns each dab by a position-keyed amount: the stroke changes, but a stroke split
// into batches still paints exactly the whole stroke
let jittery = paint.brush({size: 24.0, tip: chalk, spacing: 0.2, angle_jitter: 180.0})^
let steady = paint.brush({size: 24.0, tip: chalk, spacing: 0.2})^
let long = [[10.0, 30.0, 1.0], [50.0, 26.0, 1.0], [110.0, 34.0, 1.0]]
let whole = paint.stroke_step(blank, jittery, ink, long)^.layer
let first = paint.stroke_step(blank, jittery, ink, [long[0], long[1]])^
let split = paint.stroke_step(blank, jittery, ink, [long[2]], first)^.layer
check("jitter-turns-dabs", whole != paint.stroke_step(blank, steady, ink, long)^.layer)
check("jitter-split-equals-whole", split == whole)
check("bad-jitter", match (paint.brush({angle_jitter: 200.0}) ^ { ^ }) { case error: true default: false })

// a session keeps its texture spec (proc/paint_session.ls checks that strokes read it)
let s = paint.session(120, 60, {texture: {kind: 'cold_press', seed: 3, size: 64}})^
check("session-texture", s.texture == cold and s.texture_spec.kind == 'cold_press')
check("with-texture", paint.with_texture(s, null)^.texture == null and paint.with_texture(s, {kind: 'canvas', size: 32})^.texture_spec.size == 32)

check("bad-paper", match (paint.paper('vellum') ^ { ^ }) { case error: true default: false })
check("bad-tip", match (paint.brush({tip: as_float([1.0, 0.5])}) ^ { ^ }) { case error: true default: false })
check("bad-grain", match (paint.brush({grain: 1.5}) ^ { ^ }) { case error: true default: false })
check("bad-texture", match (paint.session(10, 10, {texture: "rough"}) ^ { ^ }) { case error: true default: false })
