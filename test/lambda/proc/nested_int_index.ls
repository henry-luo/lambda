// Result37 fix (2026-09-07): a subscript that is itself a typed int[] element
// read (`vals[keys[i]]`, `flags[keys[i]]`) is a native index leaf; the inner
// read's lane null (out-of-range) fails the outer bounds check exactly as a
// boxed `xs[null]` key: null read (S7.1.1), raised write (S7.1.3v2).
pn wr_at(var v: int[], k: int) any^ { v[k] = 7 }
pn run() {
    var vals: int[] = [10, 20, 30, 40]
    var keys: int[] = [1, 3, -1, 9, 0]
    var got = []
    var i = 0
    while (i < 5) { push(got, vals[keys[i]]); i = i + 1 }
    var wr = null
    wr_at(vals, keys[3]) ^ { wr = "raised" }
    var wr2 = null
    wr_at(vals, keys[2]) ^ { wr2 = "raised" }
    wr_at(vals, keys[0])
    var m: int[] = fill(4, 2)
    var s = 0
    i = 0
    while (i < 4) { s = s + vals[m[i] * 1] + vals[i * 1]; i = i + 1 }
    var flags: bool[] = fill(4, true)
    flags[2] = false
    var t = 0
    i = 0
    while (i < 5) { if (flags[keys[i]] and (not flags[m[i % 4]])) { t = t + 1 }; i = i + 1 }
    print(got, " ", wr, " ", wr2, " ", s, " ", t, " ", vals, "\n")
}
pn main() { run() }
