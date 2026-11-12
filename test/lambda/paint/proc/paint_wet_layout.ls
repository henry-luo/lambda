// Pigment-dependent wet planes, stepping and dry transfer (PE9, D3.2.1, S9.1.2).
import paint: lambda.paint
import wc: lambda.procart.watercolor
import radiant

let Sheet = wc.Sheet
fn all_ok(a) => a.shape and a.offsets and a.moved and a.lane and a.snapshot and a.bad_shape and a.work and a.bad_deposit and a.painted and a.cleared and a.rect

pn stamp_ok(count: int) bool^ {
    var s: Sheet = paint.wet_sheet(8, 8, 3, 2.5, 0.0, count)^
    let spec = paint.wet_layout(count)^
    let pigment = {kind:"pigment", channel:count-1, x:4.0, y:4.0, radius:2.0, strength:0.7}
    let pristine = s
    wc.stamp_many(s, pigment, count)^
    let loaded = s.cells[(spec.suspended+count-1)*64+36] > 0.69
    let snapshot_safe = pristine.cells[(spec.suspended+count-1)*64+36] == 0.0
    let before = s
    let wrong = wc.stamp_many(s, pigment, count+1) ^ { ^ }
    let rejected = wrong is error and s == before
    let ads = [for (k in 0 to (count-1), v in [0.1,1.0,0.0]) v]
    paint.wet_step(s, ads, {tilt:0.0, evaporation:0.0, edges:false, drying:true}, 1)^
    let deposited = s.cells[(spec.deposited+count-1)*64+36]
    wc.stamp_many(s, {kind:"water", channel:0, x:4.0, y:4.0, radius:2.0, strength:1.0}, count)^
    let remaining = s.cells[(spec.deposited+count-1)*64+36]
    let lifted = s.cells[(spec.suspended+count-1)*64+36]
    wc.stamp_many(s, {kind:"lift", channel:0, x:4.0, y:4.0, radius:2.0, strength:1.0}, count)^
    return loaded and snapshot_safe and rejected and deposited > 0.6 and deposited < 0.7 and remaining < deposited and lifted > 0.0 and
        s.cells[(spec.deposited+count-1)*64+36] < remaining and s.cells[(spec.suspended+count-1)*64+36] < lifted
}

pn exercise(count: int) map^ {
    let n = 64, centre = 36
    let spec = paint.wet_layout(count)^
    let base = paint.wet_sheet(8, 8, 3, 2.5, 0.0, count)^
    let fixed = [for (i in 0 to (8*n - 1)) if (i < n) 0.5 else if (i == n + centre) 0.5 else 0.0]
    let suspended = [for (k in 0 to (count - 1), i in 0 to (n - 1)) if (i == centre) 0.4 else 0.0]
    var s = {*:base, cells:[*fixed, *suspended, *fill((2*count+4)*n, 0.0)^],
        x0:0, y0:0, x1:7, y1:7, wet_diffusion:0.0, pigment_diffusion:0.0}
    let ads = [for (k in 0 to (count - 1), value in [0.1, 1.0, 0.0]) value]
    let pigments = [for (k in 0 to (count - 1)) wc.CLASSIC[k % 3]]
    let before = s
    var narrow = {*:s, cells:radiant.paint_wet_cells(s.cells)^}
    let step = paint.wet_step(s, ads, {tilt:0.0, evaporation:1.0, edges:false, drying:false}, 1)^
    paint.wet_step(narrow, ads, {tilt:0.0, evaporation:1.0, edges:false, drying:false}, 1)^
    let f32_ok = all([for (i in 0 to (len(narrow.cells)^-1)) abs(float(narrow.cells[i]) - float(s.cells[i])) < 0.00001]) ^ { false }
    let all_moved = all([for (k in 0 to (count - 1))
        abs(s.cells[(spec.suspended+k)*n+centre] - 0.36) < 0.00001 and
        abs(s.cells[(spec.deposited+k)*n+centre] - 0.04) < 0.00001])
    let wrong = paint.wet_step(s, [0.1,1.0,0.0,0.1,1.0,0.0], wc.SETTINGS, 1) ^ { ^ }
    paint.wet_step(s, ads, {tilt:0.0, evaporation:0.0, edges:false, drying:true}, 1)^
    var layer = paint.layer(8, 8, {format:'pigment'})^
    let before_bad = s
    let before_layer = layer
    let bad_deposit = paint.wet_deposit(layer, s, wc.CLASSIC) ^ { ^ }
    let rejected_unchanged = s == before_bad and layer == before_layer
    let report = paint.wet_deposit(layer, s, pigments)^
    let cleared = all([for (k in 0 to (count - 1)) s.cells[(spec.deposited+k)*n+centre] == 0.0])
    return {shape:len(base.cells) == spec.planes*n and spec.planes == 12 + 3*count,
        offsets:spec.suspended == 8 and spec.deposited == 8+count and spec.scratch == 8+2*count,
        moved:all_moved, lane:f32_ok, snapshot:before != s, bad_shape:wrong is error,
        bad_deposit:bad_deposit is error and rejected_unchanged,
        work:step.work == n * ((count+2) div 3), painted:paint.painted_tiles(layer)^ == 1,
        cleared:cleared, rect:report.rect == [0,0,8,8]}
}

pn main() {
    let a = exercise(1)^
    let b = exercise(4)^
    let c = exercise(7)^
    let bad_zero = paint.wet_layout(0) ^ { ^ }
    let bad_many = paint.wet_layout(4097) ^ { ^ }
    let too_large = paint.wet_sheet(512, 512, 2, 2.5, 0.0, 4096) ^ { ^ }
    var budget = paint.wet_sheet(8, 8, 2, 2.5, 0.0, 7)^
    let saved = budget
    let ads = [for (k in 0 to 6, v in [0.1, 1.0, 0.0]) v]
    let over = paint.wet_step(budget, ads, wc.SETTINGS, 1000000) ^ { ^ }
    return {one:all_ok(a), four:all_ok(b), seven:all_ok(c), invalid:bad_zero is error and bad_many is error,
        storage:too_large is error, budget:over is error and budget == saved,
        stamps:stamp_ok(1)^ and stamp_ok(4)^ and stamp_ok(7)^}
}
