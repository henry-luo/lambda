// S11.1.1v3: `T[]` holds when every element satisfies T, and rank is preserved,
// so a typed boundary admits exactly what `is` accepts -- an ArrayNum view or
// N-D array included. Both tiers rejected them whenever the contract had no
// exact packed lane (`number[]`, `float[]`, `int?[]`, a union): the element-wise
// admission cloned the source, and a view's clone is the view itself. The
// admitted value is a snapshot (S9.2.2): in value position a view is a copy.

fn dyn(v) => v
fn nums(a: number[]) { [a, len(a)] }
fn grid(v) number[][] { v }

pn main() {
    var m = reshape([1, 2, 3, 4], [2, 2])
    let m3 = reshape([1, 2, 3, 4, 5, 6, 7, 8], [2, 2, 2])
    let sv = subview([1, 2, 3, 4], 1, 3)

    // rank-one views: a row, a subview, and a row of a 3-D array's row
    let a: number[] = dyn(m[0])
    let b: float[] = dyn(sv)
    let c: int?[] = dyn(m3[1][0])
    let d: (int | string)[] = dyn(m[1])
    print([a, b, type(b[0]), c, d, a is number[], len(b)])
    print("\n")

    // N-D arrays, owned and a view, keep their rank
    let e: number[][] = dyn(m)
    let f: float[][] = dyn(m3[0])
    let g: int?[][] = dyn(m3[1])
    let h: (int | string)[][] = dyn(m)
    print([e, len(e), f, type(f[0][0]), g, h[1], e is number[][]])
    print("\n")

    // parameters, returns, and reassignment of a declared `var`
    var r: (int | string)[] = ["s"]
    r = dyn(m[1])
    print([nums(m[0]), grid(m3[1]), r])
    print("\n")

    // each admitted value is a snapshot: later writes on either side stay local
    var w: number[] = dyn(m[0])
    w[1] = 2.5
    m[0][0] = 99
    r[0] = "t"
    print([a, e[0], w, r, m])
    print("\n")

    // an exact lane still admits a view in place, and `len` counts rows on
    // every tier: the JIT read an annotated N-D binding's leaf count
    let p: float[][] = [[1.5, 2.5], [3.5, 4.5], [5.5, 6.5]]
    let q: int[][] = reshape([1, 2, 3, 4, 5, 6], [3, 2])
    var i = 0
    var s = 0
    while (i < len(q)) {
        s = s + q[i][1]
        i = i + 1
    }
    print([len(p), p.len(), len(q), s, [for (k in 0 to len(p) - 1) p[k][0]]])
    print("\n")
}
