// D7.6.3 / D7.6.4: a native procedure writes a `var` argument in place through the caller's home.
// radiant.paint_stroke is the in-place form of radiant.paint_dabs (Lambda_Pkg_Paint.md PT7).
import paint: lambda.paint
import radiant

pn batch() map^ {
    let pen = paint.brush({size: 10.0, hardness: 0.8, opacity: 0.3})^
    return paint.dabs(pen, [0.8, 0.1, 0.1], [[10.0, 10.0, 1.0], [80.0, 40.0, 1.0]])^
}

fn alpha(l, x: int, y: int) float^ => paint.pixel(l, x, y)^[3]

pn main() {
    let d = batch()^

    // a binding home: the caller sees the write; a snapshot taken before stays unpainted
    var layer = paint.layer(96, 64)^
    let kept = layer
    let first = radiant.paint_stroke(layer, d.table, {budget: 100000000})
    let one = alpha(layer, 40, 25)^

    // the in-place form gives the pixels of the pure form
    let pure = radiant.paint_dabs(kept, d.table, null)
    let same = paint.present(layer)^ == paint.present(pure.layer)^

    // a second batch on the same layer adds paint
    radiant.paint_stroke(layer, d.table, null)

    // a place home: the leaf is written back through its path; siblings stay
    var doc = {name: "study", layer: paint.layer(96, 64)^, strokes: 0}
    radiant.paint_stroke(doc.layer, d.table, null)
    doc.strokes = doc.strokes + 1

    // a typed binding keeps its kind, so the caller's check passes
    var typed: map = paint.layer(96, 64)^
    radiant.paint_stroke(typed, d.table, null)

    // a failed call has written nothing: the budget is charged before the first write
    let before = paint.present(layer)^
    let failed = radiant.paint_stroke(layer, d.table, {budget: 1})

    return {binding_painted: paint.painted_tiles(layer)^, snapshot_painted: paint.painted_tiles(kept)^,
        rect: first.rect, work_positive: first.work > 0, painted: one > 0.0,
        snapshot_alpha: alpha(kept, 40, 25)^, same_as_pure: same,
        second_batch_darker: alpha(layer, 40, 25)^ > one,
        place_painted: paint.painted_tiles(doc.layer)^, place_name: doc.name, place_strokes: doc.strokes,
        typed_painted: paint.painted_tiles(typed)^,
        over_budget_error: failed is error, over_budget_unchanged: paint.present(layer)^ == before}
}
