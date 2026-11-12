// lambda.paint pigment sessions (Lambda_Pkg_Paint.md PT5, PT7, PT10, PE8): wet strokes mix in place,
// a lit frame, drying, undo, bristle strokes in batches, and painting files with a pigment layer.
import paint: lambda.paint

// one stroke fed in place in batches
pn stroked(s, pointer, b, color, batches) map^ {
    var t = paint.begin(s, pointer, b, color)^
    for (samples in batches) { paint.feed(t, pointer, samples)^ }
    return paint.end(t, pointer)^
}

fn hue(c) => if (c[1] > c[0] and c[1] > c[2]) 'green' else if (c[2] > c[0]) 'blue' else if (c[0] > c[2]) 'warm' else 'grey'

pn main() {
    let oil = paint.brush({size: 20.0, hardness: 0.9, amount: 1.5})^
    let s0 = paint.session(96, 64, {medium: 'pigment', paper: [1.0, 0.98, 0.94]})^
    var shown = s0
    let blank_frame = paint.frame(shown)^
    // yellow then phthalo blue, overlapping while both are wet
    let s1 = stroked(s0, 2, oil, [0.95, 0.8, 0.1], [[[10.0, 32.0, 1.0], [40.0, 32.0, 1.0]], [[60.0, 32.0, 1.0]]])^
    let s2 = stroked(s1, 2, oil, [0.05, 0.4, 0.8], [[[45.0, 32.0, 1.0], [86.0, 32.0, 1.0]]])^
    let mixed = paint.color_at(s2, 52, 32)^
    // a lit frame widens the dirty rect by a pixel, since normals read neighbours
    var clean = s2
    paint.frame(clean)^
    let s3 = stroked(clean, 2, oil, [0.6, 0.2, 0.1], [[[30.0, 12.0, 1.0], [50.0, 12.0, 1.0]]])^
    var lit = s3
    let f = paint.frame(lit)^
    var flat = paint.with_lighting(s3, false)^
    paint.frame(flat)^
    flat = stroked(flat, 2, oil, [0.6, 0.2, 0.1], [[[30.0, 12.0, 1.0], [50.0, 12.0, 1.0]]])^
    let unlit = paint.frame(flat)^
    // drying keeps every pixel; a stroke over dry paint glazes instead of mixing
    var dried = s2
    let d = paint.dry(dried)^
    let glazed = stroked(dried, 2, paint.brush({size: 20.0, hardness: 0.9, amount: 0.15})^, [0.9, 0.1, 0.1], [[[10.0, 30.0, 1.0], [86.0, 30.0, 1.0]]])^
    let wet_glaze = stroked(s2, 2, paint.brush({size: 20.0, hardness: 0.9, amount: 0.15})^, [0.9, 0.1, 0.1], [[[10.0, 30.0, 1.0], [86.0, 30.0, 1.0]]])^
    // undo returns the layer before the last stroke
    let undone = paint.undo(s2)^
    // a bristle stroke fed in two batches equals the pure stroke of the whole path
    let hairs = paint.brush({kind: 'bristle', size: 20.0, count: 10, depletion: 0.004})^
    let path = [for (i in 0 to 16) [8.0 + 5.0 * i, 48.0, 1.0]]
    let fed = stroked(s0, 2, hairs, [0.7, 0.2, 0.1], [[for (i in 0 to 7) path[i]], [for (i in 8 to 16) path[i]]])^
    let pure = paint.stroke_step(s0.layer, hairs, [0.7, 0.2, 0.1], path)^
    // clearing keeps the medium and paper
    let cleared = paint.clear(s2)^
    // a painting file keeps the pigment layer: bands, substrate and every film sample
    let root = "temp/paint_pigment_session-" ++ string(sys.proc.self.pid#)
    io.mkdir(root)^
    let file = root ++ "/oil.painting"
    paint.save_painting(s2, file)^
    let back = paint.open_painting(file)^
    return {
        layer: [s0.layer.format, s0.layer.bands, s0.layer.substrate[0] == 1.0, s0.lighting != null],
        blank_frame: blank_frame.rect,
        mixed: hue(mixed),
        patches: [hue(paint.color_at(s2, 15, 32)^), hue(paint.color_at(s2, 80, 32)^)],
        frames: [f.rect, unlit.rect, f.rect[0] == unlit.rect[0] - 1 and f.rect[2] == unlit.rect[2] + 1],
        dry: [d.work > 0, paint.color_at(dried, 52, 32)^ == mixed, paint.color_at(dried, 5, 5)^ == paint.color_at(s2, 5, 5)^],
        glaze: paint.color_at(glazed, 52, 30)^ != paint.color_at(wet_glaze, 52, 30)^,
        undo: [paint.color_at(undone, 80, 32)^ == paint.color_at(s1, 80, 32)^, paint.can_redo(undone)],
        bristles: [paint.present(fed.layer)^ == paint.present(pure.layer)^, paint.painted_tiles(fed.layer) > 0],
        cleared: [cleared.layer.format, paint.painted_tiles(cleared.layer), paint.can_undo(cleared)],
        file: [back.layer.format, back.layer.substrate == s2.layer.substrate, back.layer.tiles == s2.layer.tiles,
            paint.flattened(back)^ == paint.flattened(s2)^]
    }
}
