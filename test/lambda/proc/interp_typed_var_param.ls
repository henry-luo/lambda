// D8.1.1v8: typed `var` parameters cross the tier boundary. Every callee here
// is entered more than the promotion threshold, so on the auto tier the
// T0 -> satellite, satellite -> satellite and satellite -> T0 edges all occur;
// the golden is tier-agreed with jit and interp.
type Rec = { pos: int, count: int }
pn store_f(var x: float[], i: int, v: float) { x[i] = v }
pn store_i(var x: int[], i: int, v: int) { x[i] = v }
pn field(var r: Rec) { r.pos = r.pos + 1; r.count = r.count + 10 }
pn nested(var x: float[], i: int) { store_f(x, i, 7.5); x[0] = x[0] + 1.0 }
pn scale(var x: float[], f: float) {
    var i = 0
    while (i < len(x)) { x[i] = x[i] * f; i = i + 1 }
}
pn fromalias(var x: float[]) {
    let keep = x
    store_f(x, 1, 100.0)
    print("alias ", keep[1], " ", x[1], "\n")
}
pn shared_then_write(var x: float[], i: int) {
    let keep = x
    x[i] = -5.0
    print("shared ", keep[i], " ", x[i], "\n")
}
pn rec(var fs: float[], k: int) {
    if (k < 8) {
        store_f(fs, k % 4, float(k))
        rec(fs, k + 1)
    }
}
pn main() {
    var fs: float[] = fill(4, 0.0)
    var lit: float[] = [1.0, 2.0, 3.0]
    var is: int[] = fill(4, 0)
    var r: Rec = { pos: 1, count: 2 }
    var k = 0
    while (k < 8) {
        store_f(fs, k % 4, float(k))
        store_i(is, k % 4, k * 2)
        field(r)
        nested(fs, 2)
        scale(lit, 2.0)
        k = k + 1
    }
    print(fs, " ", is, " ", lit, " ", r.pos, " ", r.count, "\n")
    var m = 0
    while (m < 6) { fromalias(fs); shared_then_write(fs, m % 4); m = m + 1 }
    print(fs, "\n")
    var rs: float[] = fill(4, 0.0)
    rec(rs, 0)
    print(rs, "\n")
}
