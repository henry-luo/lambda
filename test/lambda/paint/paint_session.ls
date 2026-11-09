// lambda.paint interactive sessions (Lambda_Pkg_Paint.md PT10): strokes fed in batches, the dirty
// rectangle presented once, stroke-level undo and redo, and the colour under a pixel.
import paint: lambda.paint
fn check(label, value) => value

let pen = paint.brush({size: 10.0, hardness: 1.0})^
let red = [1.0, 0.0, 0.0]
let s0 = paint.session(96, 64)^
let first = paint.frame(s0)^
check("first-frame-is-whole", first.rect == [0, 0, 96, 64] and shape(first.pixels) == [64, 96, 4] and
    first.pixels[10][10] == [255, 255, 255, 255])
check("nothing-left", paint.frame(first.session)^.rect == null)

// one stroke in two batches; each frame presents only what changed since the last one
let a = paint.begin(first.session, 1, pen, red)^
let b = paint.feed(a, 1, [[10.0, 20.0, 1.0], [30.0, 20.0, 1.0]])^
let fb = paint.frame(b)^
check("stroke-rect", fb.rect[0] < 10 and fb.rect[2] > 30 and fb.rect[1] < 20 and fb.rect[3] > 20 and fb.rect[2] < 96)
check("stroke-pixels", fb.pixels[20 - fb.rect[1]][20 - fb.rect[0]] == [255, 0, 0, 255])
let c = paint.feed(fb.session, 1, [[50.0, 20.0, 1.0]])^
let fc = paint.frame(c)^
check("second-batch-rect", fc.rect[0] > 20 and fc.rect[2] > 50)
let done = paint.end(fc.session, 1)^
check("one-undo-entry", paint.can_undo(done) and not paint.can_redo(done) and len(done.undo) == 1)
check("painted", paint.color_at(done, 40, 20)^ == [1.0, 0.0, 0.0] and paint.color_at(done, 40, 50)^ == [1.0, 1.0, 1.0])

// undo restores the earlier layer and marks the stroke's area; redo puts it back
let u = paint.undo(done)^
let fu = paint.frame(u)^
check("undo", paint.color_at(u, 40, 20)^ == [1.0, 1.0, 1.0] and paint.can_redo(u) and fu.rect[0] < 10 and fu.rect[2] > 50)
let r = paint.redo(fu.session)^
check("redo", paint.color_at(r, 40, 20)^ == [1.0, 0.0, 0.0] and r.layer == done.layer and not paint.can_redo(r))

// a new stroke after undo drops the redo branch; a cancelled stroke leaves no entry
let branch = paint.end(paint.feed(paint.begin(u, 2, pen, [0.0, 0.0, 1.0])^, 2, [[10.0, 40.0, 1.0], [40.0, 40.0, 1.0]])^, 2)^
check("branch-drops-redo", not paint.can_redo(branch) and len(branch.undo) == 1 and paint.color_at(branch, 20, 40)^ == [0.0, 0.0, 1.0])
let cancelled = paint.cancel(paint.feed(paint.begin(branch, 1, pen, red)^, 1, [[60.0, 50.0, 1.0], [80.0, 50.0, 1.0]])^, 1)^
check("cancel", cancelled.layer == branch.layer and len(cancelled.undo) == 1 and paint.frame(cancelled)^.rect[0] < 60)

// a press from another pointer ends the stroke in progress first; undo during a stroke cancels it
let p1 = paint.feed(paint.begin(branch, 1, pen, red)^, 1, [[60.0, 10.0, 1.0], [80.0, 10.0, 1.0]])^
let p2 = paint.begin(p1, 2, pen, red)^
check("second-pointer-commits", len(p2.undo) == 2 and p2.active.pointer == 2)
check("undo-cancels-stroke", paint.undo(paint.feed(p2, 2, [[60.0, 30.0, 1.0], [80.0, 30.0, 1.0]])^)^.layer == p2.layer)
check("feed-needs-stroke", match (paint.feed(branch, 1, [[1.0, 1.0]]) ^ { ^ }) { case error: true default: false })

// the history budget drops the oldest entries but keeps the newest
let tight = paint.session(256, 64, {history_budget: 70000})^
fn stroke_at(s, x) map^ => paint.end(paint.feed(paint.begin(s, 1, pen, red)^, 1, [[x, 32.0, 1.0], [x + 2.0, 32.0, 1.0]])^, 1)^
let h = stroke_at(stroke_at(stroke_at(tight, 10.0)^, 100.0)^, 200.0)^
check("history-budget", len(h.undo) == 1)

// clear is one undo entry; paper colour shows under the paint
let cleared = paint.clear(done)^
check("clear", paint.color_at(cleared, 40, 20)^ == [1.0, 1.0, 1.0] and len(cleared.undo) == 2)
let cream = paint.session(8, 8, {paper: [1.0, 0.9, 0.8]})^
check("paper", paint.frame(cream)^.pixels[0][0] == [255, 230, 204, 255])

// brush adjustments keep the brush kind; viewport samples map into layer pixels
let wide = paint.adjust_brush(pen, {size: 30.0, opacity: 0.5})^
check("adjust-round", wide.size == 30.0 and wide.opacity == 0.5 and wide.hardness == 1.0)
let mp = paint.adjust_brush(paint.brush({kind: 'mypaint'})^, {size: 8.0})^
check("adjust-mypaint", mp.kind == 'mypaint' and mp.compiled.base_radius > 3.99 and mp.compiled.base_radius < 4.01)
let mapped = paint.in_layer([[60.0, 40.0, 0.5, 10.0, 0.0, 0.0, 1.0]], {x: 20.0, y: 20.0, width: 192.0, height: 128.0}, s0, 1.0)
check("in-layer", mapped == [[20.0, 10.0, 1.0, 10.0, 0.0, 0.0, 1.0]])
