// lambda.paint interactive sessions (Lambda_Pkg_Paint.md PT7, PT10): strokes fed in place in
// batches, the dirty rectangle presented once, stroke-level undo and redo, and the colour under a
// pixel. A session value taken before a feed keeps its pixels (S9.1.2).
import paint: lambda.paint

// one stroke fed in place in one batch
pn stroked(s, pointer, b, color, samples) map^ {
    var t = paint.begin(s, pointer, b, color)^
    paint.feed(t, pointer, samples)^
    return paint.end(t, pointer)^
}

pn main() {
    let pen = paint.brush({size: 10.0, hardness: 1.0})^
    let red = [1.0, 0.0, 0.0]
    let s0 = paint.session(96, 64)^
    // frame presents the dirty rect and clears it in place (PT7)
    var shown = s0
    let first = paint.frame(shown)^
    let again = paint.frame(shown)^

    // one stroke in two batches; each frame presents only what changed since the last one
    var s = paint.begin(shown, 1, pen, red)^
    let begun = s
    let fed = paint.feed(s, 1, [[10.0, 20.0, 1.0], [30.0, 20.0, 1.0]])^
    let fb = paint.frame(s)^
    let second = paint.feed(s, 1, [[50.0, 20.0, 1.0]])^
    let fc = paint.frame(s)^
    let empty = paint.feed(s, 1, [])^
    let done = paint.end(s, 1)^

    // undo restores the earlier layer and marks the stroke's area; redo puts it back
    var u = paint.undo(done)^
    let undone = u
    let fu = paint.frame(u)^
    let r = paint.redo(u)^

    // a new stroke after undo drops the redo branch; a cancelled stroke leaves no entry
    let branch = stroked(u, 2, pen, [0.0, 0.0, 1.0], [[10.0, 40.0, 1.0], [40.0, 40.0, 1.0]])^
    var c = paint.begin(branch, 1, pen, red)^
    paint.feed(c, 1, [[60.0, 50.0, 1.0], [80.0, 50.0, 1.0]])^
    let cancelled = paint.cancel(c, 1)^
    var cancelled_view = cancelled
    let cancel_frame = paint.frame(cancelled_view)^

    // a press from another pointer ends the stroke in progress first; undo during a stroke cancels it
    var p = paint.begin(branch, 1, pen, red)^
    paint.feed(p, 1, [[60.0, 10.0, 1.0], [80.0, 10.0, 1.0]])^
    let p2 = paint.begin(p, 2, pen, red)^
    var q = p2
    paint.feed(q, 2, [[60.0, 30.0, 1.0], [80.0, 30.0, 1.0]])^

    // feeding without a stroke fails and leaves the session as it was
    var idle = branch
    let refused = paint.feed(idle, 1, [[1.0, 1.0]]) ^ { ^ }

    // the history budget drops the oldest entries but keeps the newest
    var h = paint.session(256, 64, {history_budget: 70000})^
    for (x in [10.0, 100.0, 200.0]) {
        h = stroked(h, 1, pen, red, [[x, 32.0, 1.0], [x + 2.0, 32.0, 1.0]])^
    }

    // a session's paper texture reaches every stroke it feeds
    let cold = paint.paper('cold_press', {size: 64, seed: 3})^
    let gritty = paint.brush({size: 24.0, hardness: 0.9, grain: 1.0})^
    let ink = [0.1, 0.2, 0.6]
    let path = [[20.0, 30.0, 1.0], [100.0, 30.0, 1.0]]
    let textured = paint.session(120, 60, {texture: {kind: 'cold_press', seed: 3, size: 64}})^
    let grained = paint.stroke_step(paint.layer(120, 60)^, gritty, ink, path, null, {paper: cold})^.layer

    // a wash session composites its coverage once per batch, as the pure form does
    let washer = paint.brush({size: 16.0, hardness: 0.5, opacity: 0.5, mode: 'wash'})^
    let retrace = [[20.0, 30.0, 1.0], [70.0, 30.0, 1.0], [20.0, 30.0, 1.0]]
    var w = paint.begin(paint.session(96, 64)^, 1, washer, red)^
    paint.feed(w, 1, [retrace[0], retrace[1]])^
    paint.feed(w, 1, [retrace[2]])^
    let whole_wash = paint.stroke_step(paint.layer(96, 64)^, washer, red, retrace)^.layer

    // clear is one undo entry; paper colour shows under the paint
    let cleared = paint.clear(done)^
    let cream = paint.session(8, 8, {paper: [1.0, 0.9, 0.8]})^
    var cream_view = cream
    let cream_frame = paint.frame(cream_view)^

    // brush adjustments keep the brush kind; viewport samples map into layer pixels
    let wide = paint.adjust_brush(pen, {size: 30.0, opacity: 0.5})^
    let mp = paint.adjust_brush(paint.brush({kind: 'mypaint'})^, {size: 8.0})^
    let mapped = paint.in_layer([[60.0, 40.0, 0.5, 10.0, 0.0, 0.0, 1.0]], {x: 20.0, y: 20.0, width: 192.0, height: 128.0}, s0, 1.0)

    return {
        first_frame_is_whole: first.rect == [0, 0, 96, 64] and shape(first.pixels) == [64, 96, 4] and
            first.pixels[10][10] == [255, 255, 255, 255],
        nothing_left: again.rect == null and again.pixels == null and shown.dirty == null,
        stroke_rect: fb.rect[0] < 10 and fb.rect[2] > 30 and fb.rect[1] < 20 and fb.rect[3] > 20 and fb.rect[2] < 96,
        stroke_pixels: fb.pixels[20 - fb.rect[1]][20 - fb.rect[0]] == [255, 0, 0, 255],
        feed_reports_rect: fed.rect == fb.rect and fed.work > 0,
        snapshot_unpainted: paint.color_at(begun, 20, 20)^ == [1.0, 1.0, 1.0] and begun.active.cont == null,
        second_batch_rect: fc.rect[0] > 20 and fc.rect[2] > 50 and second.rect == fc.rect,
        empty_batch: empty.rect == null and empty.work == 0,
        one_undo_entry: paint.can_undo(done) and not paint.can_redo(done) and len(done.undo) == 1,
        undo_entry_unpainted: paint.color_at({*: done, layer: done.undo[0].layer}, 40, 20)^ == [1.0, 1.0, 1.0],
        painted: paint.color_at(done, 40, 20)^ == [1.0, 0.0, 0.0] and paint.color_at(done, 40, 50)^ == [1.0, 1.0, 1.0],
        undo: paint.color_at(undone, 40, 20)^ == [1.0, 1.0, 1.0] and paint.can_redo(undone) and fu.rect[0] < 10 and fu.rect[2] > 50,
        redo: paint.color_at(r, 40, 20)^ == [1.0, 0.0, 0.0] and r.layer == done.layer and not paint.can_redo(r),
        branch_drops_redo: not paint.can_redo(branch) and len(branch.undo) == 1 and paint.color_at(branch, 20, 40)^ == [0.0, 0.0, 1.0],
        cancel: cancelled.layer == branch.layer and len(cancelled.undo) == 1 and cancel_frame.rect[0] < 60,
        second_pointer_commits: len(p2.undo) == 2 and p2.active.pointer == 2,
        undo_cancels_stroke: paint.undo(q)^.layer == p2.layer,
        feed_needs_stroke: refused is error and idle == branch,
        history_budget: len(h.undo) == 1,
        session_grain: stroked(textured, 1, gritty, ink, path)^.layer == grained,
        wash_matches_pure: w.layer == whole_wash,
        clear: paint.color_at(cleared, 40, 20)^ == [1.0, 1.0, 1.0] and len(cleared.undo) == 2,
        paper: cream_frame.pixels[0][0] == [255, 230, 204, 255],
        adjust_round: wide.size == 30.0 and wide.opacity == 0.5 and wide.hardness == 1.0,
        adjust_mypaint: mp.kind == 'mypaint' and mp.compiled.base_radius > 3.99 and mp.compiled.base_radius < 4.01,
        in_layer: mapped == [[20.0, 10.0, 1.0, 10.0, 0.0, 0.0, 1.0]]
    }
}
