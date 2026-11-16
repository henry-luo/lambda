// A uniform 32 m cell index over the near terrain grid: which items (road segments, rails, tree trunks)
// may lie near a point. Items enter by their bounding boxes in metres, [x0, z0, x1, z1].
import land: .mod_land

pub let CELL = 32.0
pub let CNX = int(ceil((land.X1 - land.X0) / CELL)) ^ { 1 }
pub let CNZ = int(ceil((land.Z1 - land.Z0) / CELL)) ^ { 1 }
pub fn cx_of(x) => int(max(0.0, min(CNX - 1, floor((x - land.X0) / CELL)))) ^ { 0 }
pub fn cz_of(z) => int(max(0.0, min(CNZ - 1, floor((z - land.Z0) / CELL)))) ^ { 0 }
// for every cell, the items whose box overlaps it
pub fn build(boxes) any^ {
    let ranges = [for (b in boxes) {x0: cx_of(b[0]), x1: cx_of(b[2]), z0: cz_of(b[1]), z1: cz_of(b[3])}]
    let pairs = [for (n, r in ranges, cx in r.x0 to r.x1, cz in r.z0 to r.z1) {cell: cz * CNX + cx, n: n}]
    let lists = map([for (p in pairs group by p.cell into g) (string(cell), g |> ~.n)]);
    [for (c in 0 to (CNX * CNZ - 1)) lists[string(c)] or []]
}
// the items in the cell under (x, z), and those in the cells within r of it (each once)
pub fn at(cells, x, z) any^ => cells[cz_of(z) * CNX + cx_of(x)]
pub fn near(cells, x, z, r) any^ => if (not land.in_near(x, z)) []
    else unique([for (cx in cx_of(x - r) to cx_of(x + r), cz in cz_of(z - r) to cz_of(z + r), n in cells[cz * CNX + cx]) n])
