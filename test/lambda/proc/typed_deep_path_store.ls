// T27-4 key span: a typed-root path store with more than three keys takes the
// same checked setter as a short one (D3.2.4v3) -- a proven root writes in
// place, a leaf needing numeric admission is converted, a mistyped leaf is
// rejected and leaves the record untouched -- without building a path array.
type Cell = {n: int, f: float, tags: int[]}
type Grid = {rows: Cell[][], label: string}
type Deep = {grid: Grid, count: int}

pn make_grid() Grid {
    var g: Grid = {rows: [[{n: 1, f: 1.5, tags: [1]}, {n: 2, f: 2.5, tags: []}],
                          [{n: 3, f: 3.5, tags: [3]}]], label: "g"}
    return g
}

// four keys through a local typed root; `f = 7` is admitted as 7.0
pn local_root() {
    var g: Grid = make_grid()
    g.rows[0][1].n = 20
    g.rows[1][0].f = 7
    print(g.rows[0][1].n) print(" ")
    print(g.rows[1][0].f) print("\n")
}

// five keys through a `var` parameter root (the in-place arm)
pn bump(var d: Deep, i: int, j: int) {
    d.grid.rows[i][j].n = d.grid.rows[i][j].n + 100
    d.grid.rows[i][j].f = 0
}
pn var_param_root() {
    var d: Deep = {grid: make_grid(), count: 0}
    var k = 0
    while (k < 3) {
        bump(d, 0, 0)
        k = k + 1
    }
    bump(d, 1, 0)
    print(d.grid.rows[0][0].n) print(" ")
    print(d.grid.rows[0][0].f) print(" ")
    print(d.grid.rows[1][0].n) print("\n")
}

// the stored copy is a snapshot: a deep write must not reach it (S9.1.2)
pn snapshot_isolation() {
    var d: Deep = {grid: make_grid(), count: 0}
    let before = d.grid.rows[0][1]
    d.grid.rows[0][1].n = 55
    print(before.n) print(" ") print(d.grid.rows[0][1].n) print("\n")
}

// a mistyped leaf fails the store and leaves the record as it was
pn set_n(var d: Deep, v: any) {
    d.grid.rows[0][0].n = v
}
pn rejected() {
    var d: Deep = {grid: make_grid(), count: 0}
    set_n(d, 8)
    let bad = set_n(d, "x") ^ { print("rejected ") }
    print(d.grid.rows[0][0].n) print("\n")
}

// array roots: a certified unique spine stores inline, anything else (a
// shared array, a mistyped leaf, a bad index) takes the checked setter
type Node = {dfn: int, parentDfn: int}
pn node_new(d: int) Node {
    var n: Node = {dfn: d, parentDfn: d}
    return n
}
pn set_parent(var nodes: Node[], i: int, p: any) {
    nodes[i].parentDfn = p
}
pn array_roots() {
    var nodes: Node[] = []
    var i = 0
    while (i < 4) {
        push(nodes, node_new(i))
        i = i + 1
    }
    set_parent(nodes, 2, 0)
    let snap = nodes
    set_parent(nodes, 3, 1)
    let bad = set_parent(nodes, 1, "x") ^ { print("rejected ") }
    let oob = set_parent(nodes, 9, 1) ^ { print("out of range ") }
    print(nodes[2].parentDfn) print(" ") print(nodes[3].parentDfn) print(" ")
    print(snap[3].parentDfn) print(" ") print(nodes[1].parentDfn) print("\n")
}

// a store index whose static carrier is a wrapper or `any` is tag-tested on
// the inline arm; anything but a packed int takes the checked setter
type Slot = {mark: int}
type Holder = {cells: Slot?[], slots: Slot[]}
pn holder_new() Holder {
    var h: Holder = {cells: [null, {mark: 4}], slots: [{mark: 0}, {mark: 0}]}
    return h
}
pn may_defect_index(var h: Holder) int {
    return h.cells[1].mark - 3
}
pn any_index(k: int) any {
    if (k == 0) { return 1 }
    if (k == 1) { return 1.0 }
    if (k == 2) { return null }
    return 7
}
pn set_slot(var h: Holder, i: any, m: int) {
    h.slots[i].mark = m
}
pn boxed_indices() {
    var h: Holder = holder_new()
    var i = may_defect_index(h)
    h.slots[i].mark = 11
    set_slot(h, any_index(0), 12)
    let f = set_slot(h, any_index(1), 13) ^ { print("float-index ") }
    let n = set_slot(h, any_index(2), 14) ^ { print("null-index ") }
    let o = set_slot(h, any_index(3), 15) ^ { print("oob-index ") }
    print(h.slots[0].mark) print(" ") print(h.slots[1].mark) print("\n")
}

pn main() {
    local_root()
    var_param_root()
    snapshot_isolation()
    rejected()
    array_roots()
    boxed_indices()
}
