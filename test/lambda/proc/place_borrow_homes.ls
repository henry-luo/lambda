// S9.1.2/S9.1.3: a place borrow publishes replacements without changing snapshots.
type Cell = {k: int}
type Box = {cell: Cell, count: int, values: int[]}

pub pn change(var cell: Cell) int {
    let saved = cell
    cell.k = 8
    return saved.k
}
pub pn replace(var cell: Cell) { cell = {k: 12} }
pn increment(var count: int) { count = count + 1 }
pn array_change(var values: int[]) int {
    let saved = values
    values[0] = 9
    return saved[0]
}
pn any_change(var cell) int {
    let saved = cell
    cell.k = 20
    return saved.k
}
pn partial(var cell: Cell) int^ {
    let saved = cell
    cell.k = 30
    raise error("place failure")
}
pn paired(var cell: Cell, saved: Box) int {
    cell.k = 44
    return saved.cell.k
}
pn at(var count: int) int { count = count + 1; return 0 }
pn array_replace(var values: int[]) { values = [6, 7] }
pub pn returned_snapshot(var cell: Cell) {
    let saved = cell
    cell = {k: 25}
    return {saved: saved, data: [1, 2, 3]}
}
pn main() {
    var b: Box = {cell: {k: 1}, count: 3, values: [4, 5]}
    print([change(b.cell), b.cell.k]); print("\n")
    replace(b.cell)^
    increment(b.count)^
    print([b.cell.k, b.count]); print("\n")
    print([array_change(b.values), b.values]); print("\n")
    print([any_change(b.cell), b.cell.k]); print("\n")
    partial(b.cell) ^ { print(b.cell.k); print("\n") }
    var rows: Cell[] = [{k: 2}, {k: 3}]
    var index = 0
    print([change(rows[index]), rows[index].k]); print("\n")
    replace(rows[index])^
    print(rows[index].k); print("\n")
    print([paired(b.cell, b), b.cell.k]); print("\n")
    var calls = 0
    print([change(rows[at(calls)]), calls, rows[0].k]); print("\n")
    array_replace(b.values)^
    print(b.values); print("\n")
    let outcome = returned_snapshot(b.cell)
    print([outcome.saved.k, outcome.data, b.cell.k]); print("\n")
    increment(b.values[last])^
    print(b.values); print("\n")
}
