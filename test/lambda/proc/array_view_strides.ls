// Typed Array 4 Scope 3: a view aliases its base, with `data` at its first
// element and the shape's strides reaching the rest. A transposed matrix is
// such a view, and so is each of its rows: row 0 of `transpose(m)` for a 2 x 3
// `m` is m's first column, whose elements sit 3 apart in m's buffer. Every
// reader of a rank-one view took the elements densely from `data`, on both
// tiers -- `t[0]` read [1, 2], its base's next elements -- and a write through
// a row landed on the wrong element of the base.

fn dyn(v) => v
fn ints(v: int[]) { [v, len(v)] }
fn floats(v: float[]) { sum(v) }

pn main() {
    let m = reshape([1, 2, 3, 4, 5, 6], [2, 3])
    let t = transpose(m)

    // rows of a transposed matrix are the columns of its base
    print([t, t[0], t[1], t[2], len(t[0])])
    print("\n")
    print([t[1][0], t[1][1], t[2, 1], [for (row in t) [for (e in row) e]]])
    print("\n")

    // every consumer walks positions through the stride
    let r = t[1]
    print([sum(r), avg(r), min(r), max(r), sort(r), reverse(r), unique(r)])
    print("\n")
    print([math.cumsum(r), math.cumprod(t[2]), math.dot(t[0], r), math.norm(t[0])])
    print("\n")
    print([r * 10, r + t[2], r == [2, 5], r ++ t[0], string(r), format(r, 'json')])
    print("\n")
    print([r[1 to 1], slice(r, 1, 2), subview(r, 1, 2), take(r, 1), drop(r, 1)])
    print("\n")

    // a transposed 3-D array: its rows are strided matrices, theirs strided rows
    let c = transpose(reshape([1, 2, 3, 4, 5, 6, 7, 8], [2, 2, 2]))
    print([c[1], c[1][0], c[1][1], sum(c[1]), c[1][0] ++ c[1][1]])
    print("\n")

    // float rows, and typed boundaries, which copy a strided view (S9.2.2)
    let tf = transpose(reshape([1.5, 2.5, 3.5, 4.5, 5.5, 6.5], [2, 3]))
    let fr = tf[2]
    let typed: int[] = dyn(t[0])
    print([fr, floats(fr), ints(t[2]), typed, dyn(t[1]) is int[]])
    print("\n")

    // a write through a strided row goes to the element it names: the row
    // read [1, 4] after `col[1] = 99`, which had landed on its base's next
    // element. Only the rows are printed -- whether a view binding writes
    // through to its base is S9.2.2's open borrow question, not this fixture's.
    var w = reshape([1, 2, 3, 4, 5, 6], [2, 3])
    let wt = transpose(w)
    var col = wt[0]
    col[1] = 99
    var sub = subview(wt[2], 1, 2)
    sub[0] = 77
    print([col, sub])
    print("\n")
}
