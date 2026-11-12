// Transferring a dry watercolor sheet changes both owners once and preserves snapshots (PE9, S9.1.2).
import wc: lambda.procart.watercolor
import paint: lambda.paint
let Sheet = wc.Sheet
pn main() {
    var ref: Sheet = wc.sheet(16, 16, 2, 2.5, 0.0)^
    wc.stamp(ref, {kind:"pigment", channel:0, x:8.0, y:8.0, radius:4.0, strength:0.7})^
    wc.step(ref, wc.adsorption(wc.CLASSIC), wc.SETTINGS)^
    var wet = paint.wet_from_reference(ref)^
    var wet_layer = paint.layer(16, 16, {format:'pigment'})^
    let original_wet = wet
    let original_layer = wet_layer
    let reject = paint.wet_deposit(wet_layer, wet, wc.CLASSIC) ^ { ^ }
    wc.dry(ref)^
    var dry = paint.wet_from_reference(ref)^
    var layer = paint.layer(16, 16, {format:'pigment'})^
    let before = dry
    let before_layer = layer
    let report = paint.wet_deposit(layer, dry, wc.CLASSIC)^
    let once = layer
    paint.wet_deposit(layer, dry, wc.CLASSIC)^
    let remaining = sum(subview(dry.cells, 11*256, 14*256)^)
    return {reject:reject is error, rejected_unchanged:wet == original_wet and wet_layer == original_layer,
        deposited:paint.pixel(layer, 8, 8)^ != [255, 255, 255, 255],
        cleared:remaining == 0.0, once:layer == once, snapshot:before != dry and before_layer != layer,
        bounds:report.rect == [0, 0, 16, 16]}
}
