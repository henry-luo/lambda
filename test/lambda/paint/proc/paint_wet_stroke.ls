// Wet dabs use the shared stroke placement and preserve value snapshots (PE9, S9.1.2).
import paint: lambda.paint
import radiant

pn main() {
    let original = paint.wet_sheet(40, 32, 2, 2.5, 0.0, 4)^
    var whole = original
    var split = original
    var narrow = {*:original, cells:radiant.paint_wet_cells(original.cells)^}
    let brush = paint.brush({size:8.0, hardness:1.0, amount:0.6})^
    let color = [0.2, 0.3, 0.8, 1.0]
    let points = [[8.5, 12.5, 1.0], [16.5, 12.5, 1.0], [24.5, 12.5, 1.0]]
    let all = paint.wet_stroke(whole, brush, color, points, 3, 0.45)^
    let first = paint.wet_stroke(split, brush, color, [points[0], points[1]], 3, 0.45)^
    let second = paint.wet_stroke(split, brush, color, [points[2]], 3, 0.45, first)^
    paint.wet_stroke(narrow, brush, color, points, 3, 0.45)^
    let n = 40 * 32, center = 12 * 40 + 8
    let diff = max([for (i in 0 to (len(whole.cells)^-1)) abs(float(whole.cells[i]) - float(narrow.cells[i]))])
    let before = whole
    let bad = paint.wet_stroke(whole, brush, color, points, 4, 0.45) ^ { ^ }
    let wrong = paint.wet_stroke(whole, brush, color, points, 2, 0.45, all) ^ { ^ }
    let bristle = paint.brush({kind:'bristle', erase:true})^
    let unsupported = paint.wet_stroke(whole, bristle, color, points, 3, 0.45) ^ { ^ }
    var dynamic = original
    let mypaint = paint.brush({kind:'mypaint', amount:0.6})^
    let my = paint.wet_stroke(dynamic, mypaint, color, points, 2, 0.45)^
    return {pigment:whole.cells[11*n+center] > 0.0, water:whole.cells[n+center] > 0.0,
        pressure:whole.cells[3*n+center] > 0.0, other:whole.cells[8*n+center] == 0.0,
        split:whole.cells == split.cells and all.work == second.work, lanes:diff < 0.00001,
        snapshot:original.cells[11*n+center] == 0.0, bounds:all.rect != null and whole.x1 >= 24,
        mypaint:my.work > 0 and dynamic.cells[10*n+center] > 0.0 and dynamic.cells[n+center] > 0.0,
        invalid:bad is error and wrong is error and unsupported is error and whole == before}
}
