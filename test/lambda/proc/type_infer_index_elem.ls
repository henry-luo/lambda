// TIG1 + defect-1 regression [Type_Infer Impl §15].
// An indexed read publishes its array's element type as `T?`. The float cases
// exercise the emitter path that used to reinterpret a BOXED float through the
// double lane (`dmov`/`deq` with an i64 source); the mutable-binding case pins
// D3.3.3 — a `var` array's element contract must not be published, because a
// later store can rewrite it.
pn main() {
    let floats = [1.5, 2.5, 3.5]
    let ints = [10, 20, 30]
    let strs = ["a", "b"]

    // float element reads through every consumer that touches the double lane
    let sum = floats[2] + 0.5
    let txt = string(floats[0])
    let prod = floats[1] * 2.0

    // int and string element reads
    let i0 = ints[0] + 1
    let s0 = strs[1]

    // out-of-bounds stays total-null (S7.1), which is what `T?` records
    let oob = floats[9]

    // a mutable array's elements may be rewritten, so its element type is not
    // published and this must remain assignable
    var holes = [null, null]
    holes[0] = 7
    var taken: int = holes[0]

    print([sum, txt, prod, i0, s0, oob, taken])
}
