// lambda.procart.watercolor procedures: stamps, time steps, drying, edge darkening, granulation,
// tilt and the Kubelka-Munk render. Runs replay exactly; the physical checks are comparative.
import wc: lambda.procart.watercolor

let Sheet = wc.Sheet
let W = 80, H = 60

fn r4(v) => round(v * 10000.0) / 10000.0
fn layer(s, base: int) array^ => wc.plane(s, base)^ + wc.plane(s, base + 1)^ + wc.plane(s, base + 2)^
fn total(s) float^ => sum(layer(s, wc.SUSPENDED)^) + sum(layer(s, wc.DEPOSITED)^)
fn mean(a) => sum(a) / float(len(a))
fn corr(a, b) => (let da = a - mean(a), let db = b - mean(b), sum(da * db) / sqrt(sum(da * da) * sum(db * db)))
// the plane's values inside a rectangle of cells
fn region(a, x0: int, x1: int, y0: int, y1: int) => [for (y in y0 to y1, x in x0 to x1) a[y * W + x]]
fn reach(t) int^ => len([for (v in layer(t, wc.SUSPENDED)^ + layer(t, wc.DEPOSITED)^ where v > 0.001) v])
fn centroid_y(a) => sum([for (i, v in a) v * float(i div W)]) / sum(a)
// the wash's interior and the band just inside its edge
fn core(t) float^ => mean(region(wc.plane(t, wc.DEPOSITED)^, 30, 50, 22, 38))
fn rim(t) float^ => mean([*region(wc.plane(t, wc.DEPOSITED)^, 17, 20, 12, 48), *region(wc.plane(t, wc.DEPOSITED)^, 60, 63, 12, 48)])
fn dab(kind, channel: int, x: float, y: float, r: float, strength: float) map =>
    {kind:kind,channel:channel,x:x,y:y,radius:r,strength:strength}
// a block wash of dabs, three cells apart
fn wash(channel: int, strength: float) array =>
    [for (y in 0 to 8, x in 0 to 12) dab("pigment", channel, 22.0 + 3.0 * float(x), 16.0 + 3.0 * float(y), 5.0, strength)]

pn paint(brushes, steps: int, settings, wetness: float = 0.0) any^ {
    var s: Sheet = wc.sheet(W, H, 11, 2.5, wetness)^
    for (b in brushes) { wc.stamp(s, b)^ }
    let adsorption: float[] = wc.adsorption(wc.CLASSIC)
    var k: int = 0
    while (k < steps) {
        wc.step(s, adsorption^, settings)^
        k = k + 1
    }
    return s
}

pn main() {
    let calm = wc.SETTINGS
    let fast = {*:calm,evaporation:0.97}
    var results = []

    // 1 stamps: pigment lands in its own plane with water and pressure; the region grows around it
    let one = paint([dab("pigment", 0, 40.0, 30.0, 6.0, 0.7)], 0, calm)^
    let centre = 30 * W + 40
    results = results ++ [["stamp", r4(wc.plane(one, wc.SUSPENDED)^[centre]), sum(wc.plane(one, wc.SUSPENDED + 1)^) == 0.0,
        r4(wc.plane(one, wc.WET)^[centre]), r4(wc.plane(one, wc.PRESSURE)^[centre]), [one.x0, one.x1, one.y0, one.y1]]]

    // 2 time steps replay exactly; on a wetted sheet the stroke bleeds outward, on dry paper its
    // edge holds. Semi-Lagrangian transport with the divergence correction only approximately
    // conserves pigment (Washes notes the drift), so the totals are recorded, not asserted.
    let wet_a = paint([dab("pigment", 2, 40.0, 30.0, 6.0, 0.7)], 40, calm, 0.62)^
    let wet_b = paint([dab("pigment", 2, 40.0, 30.0, 6.0, 0.7)], 40, calm, 0.62)^
    let dry_a = paint([dab("pigment", 2, 40.0, 30.0, 6.0, 0.7)], 40, calm)^
    let start = paint([dab("pigment", 2, 40.0, 30.0, 6.0, 0.7)], 0, calm)^
    results = results ++ [["spread", wet_a.cells == wet_b.cells, reach(wet_a)^ > reach(start)^, reach(dry_a)^ <= reach(start)^ + 40,
        r4(total(wet_a)^ / total(start)^), r4(total(dry_a)^ / total(start)^), wet_a.steps]]

    // 3 drying locks the suspended pigment into the deposit and empties the active region
    let dried = paint(wash(1, 0.4), 200, fast)^
    results = results ++ [["drying", dried.wet_cells, sum(layer(dried, wc.SUSPENDED)^), r4(sum(layer(dried, wc.DEPOSITED)^) / total(dried)^),
        dried.x1 < dried.x0]]

    // 4 dry, damp, wet: the hair dryer settles everything; a damp level keeps pigment suspended
    var s: Sheet = paint(wash(0, 0.4), 10, calm)^
    let held = sum(layer(s, wc.SUSPENDED)^)
    wc.dry(s, 0.035)^
    let damp = [r4(max(wc.plane(s, wc.WET)^)), s.wet_cells, r4(sum(layer(s, wc.SUSPENDED)^) / held)]
    wc.dry(s)^
    let settled = [max(wc.plane(s, wc.WET)^), sum(layer(s, wc.SUSPENDED)^), s.wet_cells]
    wc.wet_sheet(s)^
    results = results ++ [["dry", damp, settled, r4(min(wc.plane(s, wc.WET)^)), s.wet_cells]]

    // 5 clear water re-suspends deposited pigment; a blot lifts from both layers
    let deposit = wc.plane(s, wc.DEPOSITED)^[25 * W + 40]
    wc.stamp(s, dab("water", 0, 40.0, 25.0, 5.0, 1.0))^
    let lifted = [wc.plane(s, wc.DEPOSITED)^[25 * W + 40] < deposit, wc.plane(s, wc.SUSPENDED)^[25 * W + 40] > 0.0]
    let pigment = total(s)^
    wc.stamp(s, dab("lift", 0, 40.0, 25.0, 5.0, 1.0))^
    results = results ++ [["water and blot", lifted, total(s)^ < pigment]]

    // 6 edge darkening carries pigment outward: a dried wash is darker at its rim and lighter inside
    // with it on (small islands are deliberately attenuated, so this uses a whole wash)
    let rimmed = paint(wash(0, 0.4), 200, fast)^
    let flat = paint(wash(0, 0.4), 200, {*:fast,edges:false})^
    results = results ++ [["edges", rim(rimmed)^ > rim(flat)^, core(rimmed)^ < core(flat)^,
        r4(rim(rimmed)^ / core(rimmed)^), r4(rim(flat)^ / core(flat)^)]]

    // 7 granulation: rose (gamma 0.81) gathers in the paper's hollows far more than hansa yellow (0.08)
    let rose = rimmed
    let yellow = dried
    let h = region(wc.plane(rose, wc.PAPER)^, 26, 54, 20, 40)
    let granular = corr(h, region(wc.plane(rose, wc.DEPOSITED)^, 26, 54, 20, 40))
    let smooth = corr(h, region(wc.plane(yellow, wc.DEPOSITED + 1)^, 26, 54, 20, 40))
    results = results ++ [["granulation", granular < 0.0, granular < smooth, r4(granular), r4(smooth)]]

    // 8 a tilted sheet drains the wash downhill
    let level = paint(wash(2, 0.4), 120, calm)^
    let tilted = paint(wash(2, 0.4), 120, {*:calm,tilt:1.0})^
    results = results ++ [["tilt", centroid_y(layer(tilted, wc.SUSPENDED)^ + layer(tilted, wc.DEPOSITED)^) >
        centroid_y(layer(level, wc.SUSPENDED)^ + layer(level, wc.DEPOSITED)^)]]

    // 9 rendering: bare paper is the cream with its grain tint; pigment absorbs; water shows blue
    var rgba: float[] = wc.canvas(one)^
    wc.render(rgba, one, wc.optics(wc.CLASSIC))^
    let p0 = wc.plane(one, wc.PAPER)^[0]
    let corner = [r4(rgba[0] - (wc.PAPER_COLOR[0] + (p0 - 0.5) * 0.06)), rgba[3]]
    let painted = [r4(rgba[centre * 4]), r4(rgba[centre * 4 + 1]), r4(rgba[centre * 4 + 2])]
    var shown: float[] = wc.canvas(one)^
    wc.render(shown, one, wc.optics(wc.CLASSIC), true, true)^
    let px = wc.pixels(one, rgba)
    results = results ++ [["render", min(rgba) >= 0.0, max(rgba) <= 1.0, corner, painted, painted[1] < painted[0],
        shown[centre * 4 + 2] > rgba[centre * 4 + 2], shape(px), px[30][40]]]

    // 10 swatches go through the same optics
    results = results ++ [["swatches", wc.swatches(wc.CLASSIC)^, wc.swatches(wc.GOUACHE)^]]
    return results
}
