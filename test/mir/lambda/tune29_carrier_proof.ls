// T29-2 (D3.2.4v4, D3.2.6): a typed path store through a declared record and a
// certified record array proves each step once. The root binding is non-null
// and carries its layout; a required array field is non-null; an element of a
// certified array carries its record layout, so no step compares a shape
// pointer. An optional element still tests for null. A root without a
// declared contract never reaches this store: it takes the generic setter.
type Cell = {k: int, n: int}
type Grid = {cells: Cell?[], size: int}

pn set_cell(var g: Grid, i: int, v: int) {
    g.cells[i].k = v
}

pn make_grid() Grid {
    return {cells: [{k: 0, n: 0}], size: 1}
}

pn set_inferred(v: int) int {
    var g = make_grid()
    g.cells[0].k = v
    return g.cells[0].k
}

pn main() {
    var g: Grid = {cells: [{k: 1, n: 0}, {k: 2, n: 0}], size: 2}
    set_cell(g, 1, 20)
    print(g.cells[1].k); print(" ")
    print(set_inferred(5)); print("\n")
}
