// D3.3.3v3: a certificate proves a value only while its carrier still matches.
// A push changes a counted array's length without crossing a boundary, so the
// next `int[3]` boundary re-checks it: the JIT elided that check for a binding
// declared with the same contract, and the counted certificate stayed valid.
fn takes3(x: int[3]) { len(x) }
pn main() {
    var a: int[3] = [1, 2, 3]
    push(a, 4)
    print("bound: " ++ string(takes3(a)))
}
