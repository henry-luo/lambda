// Carrier-lane regression [Type_Infer Impl §17].
//
// Every case below feeds a node kind whose lowering MAY box into a consumer
// that wants a raw machine lane (int arithmetic, a typed map store, float
// arithmetic). A carrier mismatch on the FLOAT lane is caught by MIR's
// verifier; on the INT lane it is not — a boxed Item and an int lane are both
// i64 registers — so it surfaces only as `inf` or a wrong number. That is why
// these cases are pinned by value.
pn main() {
    let ints = [10, 20, 30]
    let floats = [1.5, 2.5, 3.5]
    var m = {n: 0}

    // if-expression arms, member read, unary, for-expression, nested content
    // block, and a call result — each into int arithmetic.
    let a = (if (1 == 1) { ints[0] } else { ints[1] }) + 1
    let b = m.n + 1
    let c = (0 - ints[2]) + 1
    let d = len([for (x in ints) x]) + 1
    let e = (let t = ints[1], t) + 1
    let f = len(ints) + 1

    // a procedural multi-statement block whose LAST value is an int: its
    // carrier is a boxed Item, and typing it `int` without the oracle's
    // CONTENT case produced `[inf, inf, inf]` from the for-expression
    // collector.
    let collected = for (x in ints) {
        var scaled = x * 2
        scaled = scaled + 1
        scaled
    }

    // the same shapes on the float lane
    let g = floats[2] + 0.5
    let h = string(floats[0])

    m.n = a
    print([a, b, c, d, e, f, collected, g, h, m.n])
}
