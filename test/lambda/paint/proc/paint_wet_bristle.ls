// Wet bristle state and sheets keep their snapshots and split exactly (PE9, PE10, S9.1.2).
import paint: lambda.paint
import radiant

pn main() {
    let initial = paint.wet_sheet(40, 32, 4, 2.5, 0.0, 4)^
    var whole = initial
    var split = initial
    var narrow = {*:initial, cells:radiant.paint_wet_cells(initial.cells)^}
    let brush = paint.brush({kind:'bristle', size:8.0, count:4, width:2.0, amount:0.6,
        load_jitter:0.0, pickup:0.4, depletion:0.01})^
    let color = [0.1,0.3,0.9,1.0]
    let points = [[8.5,12.5,1.0], [16.5,12.5,1.0], [24.5,12.5,1.0]]
    let all = paint.wet_stroke(whole, brush, color, points, 3, 0.45)^
    let first = paint.wet_stroke(split, brush, color, [points[0],points[1]], 3, 0.45)^
    let second = paint.wet_stroke(split, brush, color, [points[2]], 3, 0.45, first)^
    paint.wet_stroke(narrow, brush, color, points, 3, 0.45)^
    let n = 40*32
    let diff = max([for (i in 0 to (len(whole.cells)^-1)) abs(float(whole.cells[i])-float(narrow.cells[i]))])
    let deposited = sum([for (i in 0 to (n-1)) whole.cells[11*n+i]])
    let water = sum([for (i in 0 to (n-1)) whole.cells[n+i]])
    let before = whole
    let eraser = paint.brush({kind:'bristle', erase:true})^
    let rejected = paint.wet_stroke(whole, eraser, color, points, 3, 0.45) ^ { ^ }
    return {painted:deposited > 0.0 and water > 0.0, other:sum([for (i in 0 to (n-1)) whole.cells[8*n+i]]) == 0.0,
        split:whole.cells == split.cells and all.bristles == second.bristles and all.work == second.work,
        lanes:diff < 0.00001, snapshot:sum([for (i in 0 to (n-1)) initial.cells[11*n+i]]) == 0.0,
        bounds:all.rect != null and whole.x1 >= 24, invalid:rejected is error and whole == before}
}
