// lambda.paint in-place strokes (Lambda_Pkg_Paint.md PT7): paint.stroke(var layer, ...) writes a
// whole stroke into the layer and gives the pixels of the pure form, paint.stroke_step. A value
// taken before the stroke keeps its pixels (S9.1.2), and a failed stroke writes nothing.
import paint: lambda.paint

// the in-place stroke of a copy of `l`, with its report
pn painted(l, b, color, points, options) map^ {
    var t = l
    let report = paint.stroke(t, b, color, points, options)^
    return {layer: t, rect: report.rect, work: report.work}
}

pn main() {
    let blank = paint.layer(160, 80)^
    let red = [1.0, 0.0, 0.0]
    let line = [[20.0, 30.0, 1.0], [140.0, 50.0, 0.6]]
    let retrace = [[20.0, 40.0, 1.0], [120.0, 40.0, 1.0], [20.0, 40.0, 1.0]]
    let hard = paint.brush({size: 12.0, hardness: 0.9})^
    let washer = paint.brush({size: 16.0, hardness: 0.5, opacity: 0.5, mode: 'wash'})^
    let smear = paint.brush({kind: 'mypaint', settings: {smudge: 1.0, smudge_length: 0.0, opaque_multiply: 1.0}})^
    let gritty = paint.brush({size: 24.0, hardness: 0.9, grain: 1.0})^
    let cold = paint.paper('cold_press', {size: 64, seed: 3})^

    // build-up, wash, smudge and paper grain each give the pixels and report of the pure form
    let built = painted(blank, hard, red, line, {})^
    let built_pure = paint.stroke_step(blank, hard, red, line)^
    let washed = painted(blank, washer, red, retrace, {})^
    let washed_pure = paint.stroke_step(blank, washer, red, retrace)^
    let base = built.layer
    let smeared = painted(base, smear, [0.0, 0.0, 1.0], [[60.0, 40.0, 1.0], [120.0, 40.0, 1.0]], {})^
    let smeared_pure = paint.stroke_step(base, smear, [0.0, 0.0, 1.0], [[60.0, 40.0, 1.0], [120.0, 40.0, 1.0]])^
    let grained = painted(blank, gritty, red, line, {paper: cold})^
    let grained_pure = paint.stroke_step(blank, gritty, red, line, null, {paper: cold})^

    // a second stroke over the first writes into the same layer; a value kept before it is unchanged
    var l = built.layer
    let kept = l
    let over = paint.stroke(l, hard, [0.0, 0.0, 1.0], [[20.0, 50.0, 1.0], [140.0, 30.0, 1.0]])^
    let over_pure = paint.stroke_step(kept, hard, [0.0, 0.0, 1.0], [[20.0, 50.0, 1.0], [140.0, 30.0, 1.0]])^

    // invalid input fails before any write
    var f = built.layer
    let bad_colour = paint.stroke(f, hard, [2.0, 0.0, 0.0], line) ^ { ^ }
    let bad_option = paint.stroke(f, hard, red, line, {pressure: 1.0}) ^ { ^ }

    return {
        build_up: built.layer == built_pure.layer and built.rect == built_pure.bounds and built.work == built_pure.work,
        build_up_painted: paint.painted_tiles(built.layer)^ > 0 and paint.painted_tiles(blank)^ == 0,
        wash: washed.layer == washed_pure.layer and washed.rect == washed_pure.bounds,
        smudge: smeared.layer == smeared_pure.layer and paint.painted_tiles(base)^ == paint.painted_tiles(built.layer)^,
        grain: grained.layer == grained_pure.layer,
        second_stroke: l == over_pure.layer and over.rect == over_pure.bounds,
        kept_unchanged: kept == built.layer and kept != l,
        bad_colour: bad_colour is error and f == built.layer,
        bad_option: bad_option is error and f == built.layer
    }
}
