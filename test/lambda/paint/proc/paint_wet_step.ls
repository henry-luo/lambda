// Native wet stepping agrees with the watercolor reference and preserves snapshots (PT14, PE9, S9.1.2).
import wc: lambda.procart.watercolor
import paint: lambda.paint
import radiant

let Sheet = wc.Sheet
fn maximum_difference(a, b) => max([for (i in 0 to (len(a)-1)) abs(float(a[i]) - float(b[i]))])

pn main() {
    var original: Sheet = wc.sheet(40, 32, 2, 2.5, 0.0)^
    wc.stamp(original, {kind:"pigment", channel:0, x:16.0, y:15.0, radius:6.0, strength:0.7})^
    wc.stamp(original, {kind:"pigment", channel:1, x:23.0, y:14.0, radius:5.0, strength:0.5})^
    var native = paint.wet_from_reference(original)^
    var split = paint.wet_from_reference(original)^
    var narrow = {*:original, cells:radiant.paint_wet_cells(original.cells)^}
    let before = native.cells
    let adsorption = wc.adsorption(wc.CLASSIC)
    let report = paint.wet_step(native, adsorption, wc.SETTINGS, 10)^
    paint.wet_step(split, adsorption, wc.SETTINGS, 4)^
    paint.wet_step(split, adsorption, wc.SETTINGS, 6)^
    paint.wet_step(narrow, adsorption, wc.SETTINGS, 10)^
    var i: int = 0
    while (i < 10) { wc.step(original, adsorption, wc.SETTINGS)^; i = i + 1 }
    let failed = paint.wet_step(native, [0.0], wc.SETTINGS, 1) ^ { ^ }
    let same_after_failure = native.cells == split.cells
    let parity = maximum_difference(native.cells, original.cells) < 0.00001
    let f32_parity = maximum_difference(narrow.cells, original.cells) < 0.00001
    let bounds = report.active == [0, 0, 40, 32]
    let split_equal = native.cells == split.cells
    paint.wet_step(native, adsorption, wc.SETTINGS, 70)^
    paint.wet_step(narrow, adsorption, wc.SETTINGS, 70)^
    i = 0
    while (i < 70) { wc.step(original, adsorption, wc.SETTINGS)^; i = i + 1 }
    var live: Sheet = paint.wet_sheet(16, 16, 2)^
    wc.stamp(live, {kind:"pigment", channel:0, x:8.0, y:8.0, radius:4.0, strength:0.7})^
    let live_result = paint.wet_step(live, adsorption, wc.SETTINGS, 2)^
    let reference_limit = wc.sheet_bounded(16, 16, 2, 2.5, 0.0, 255) ^ { ^ }
    let native_limit = paint.wet_sheet(1001, 1000, 2) ^ { ^ }
    return {parity:parity, f32_parity:f32_parity,
        long_parity:maximum_difference(native.cells, original.cells) < 0.00001 and
            maximum_difference(narrow.cells, original.cells) < 0.00001,
        live_stamp:live.steps == 2 and live_result.work == 512,
        bounded:reference_limit is error and native_limit is error,
        split:split_equal, snapshot:before != native.cells,
        unchanged:same_after_failure, failed:failed is error,
        bounds:bounds, steps:native.steps == 80,
        wet_count:native.wet_cells == original.wet_cells}
}
