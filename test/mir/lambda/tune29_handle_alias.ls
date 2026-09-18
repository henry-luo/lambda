// Tune29 §19.1 (D4.4.4v4, S9.1.2): two spellings of one runtime place, and a
// nested handle chain rebound in one branch arm. A write through one must be
// seen through the other exactly as per-access navigation sees it, and never
// reach a value another binding holds. Run on both tiers and under forced GC.
type Cell = {k: int, n: int}
type World = {cons: Cell[], count: int}
type Arr = {l0: array}
type Holder = {arr: Arr}

pn cross(var w: World, i: int, j: int) int {
    var before = w.cons[i].k
    w.cons[j].k = before + 10
    var after = w.cons[i].k
    w.cons[i].n = after
    return before * 1000 + after
}

pn null2() array { return [null, null] }

// the havlak2 store: `c1` is rebound in one arm, so the join keeps the
// possibly-shared fact of the path that skips it
pn arr_set(var a: Arr, i0: int, i1: int, val: any) int {
    var l0 = a.l0
    var c1 = l0[i0]
    if (c1 == null) {
        var _d = 0
        c1 = null2()
    }
    c1[i1] = val
    l0[i0] = c1
    a.l0 = l0
    return 0
}

pn main() {
    var w: World = {cons: [{k: 1, n: 0}, {k: 2, n: 0}], count: 2}
    var snap = w
    print(cross(w, 0, 0)); print(" ")
    print(cross(w, 0, 1)); print(" ")
    print(w.cons); print(" ")
    print(snap.cons); print("\n")
    var a: Arr = {l0: null2()}
    arr_set(a, 0, 0, 1)
    var h: Holder = {arr: a}
    arr_set(a, 0, 1, 2)
    print(a.l0); print(" ")
    print(h.arr.l0); print("\n")
}
