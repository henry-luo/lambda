// A rectangular transfer allocates only touched tiles and leaves the other deposit for later (PE9, S9.1.2).
import wc: lambda.procart.watercolor
import paint: lambda.paint
let Sheet = wc.Sheet
pn main() {
    var ref: Sheet = wc.sheet(128, 64, 5, 2.5, 0.0)^
    wc.stamp(ref, {kind:"pigment", channel:0, x:24.0, y:24.0, radius:4.0, strength:0.7})^
    wc.stamp(ref, {kind:"pigment", channel:1, x:100.0, y:24.0, radius:4.0, strength:0.7})^
    wc.dry(ref)^
    var wet = paint.wet_from_reference(ref)^
    var layer = paint.layer(128, 64, {format:'pigment'})^
    paint.wet_deposit(layer, wet, wc.CLASSIC, [0,0,64,64])^
    let first = layer
    let first_count = paint.painted_tiles(layer)^
    let right_pending = wc.plane(wet, wc.DEPOSITED + 1)^[24*128+100] > 0.0
    paint.wet_deposit(layer, wet, wc.CLASSIC, [64,0,128,64])^
    return {first_count:first_count == 1, both_tiles:paint.painted_tiles(layer)^ == 2,
        pending:right_pending, snapshot:paint.painted_tiles(first)^ == 1,
        first_unchanged:first.tiles[0] == layer.tiles[0], second_cleared:wc.plane(wet, wc.DEPOSITED + 1)^[24*128+100] == 0.0}
}
