// LR12-24 (TE-17 I3, D6.1.3): a call whose result may be an error its
// signature does not declare -- a contained defect -- is `T | error` to its
// caller, never a native lane. The JIT had read such an error's bits as a
// number (`inf`, `nan`, `0`) wherever the call fed a lane: an operand, an
// accumulator, a literal slot or a store.

// a declaration boundary originates the defect (S7.7.2) ...
fn check(x) {
    let n: int = x
    n + 1
}
// ... and so does a declared return
fn tchk(x) int {
    let n: int = x
    n + 1
}
fn sum_twice(x) int => check(x) + check(x)
// a failed element store returns the error from its function (S7.7.6)
pn guarded_store(a: float[], i: int) {
    a[i] = 9.5
    return a[i]
}
pn acc_untyped(xs) {
    var acc = 0
    for x in xs { acc = acc + check(x) }
    acc
}
pn acc_typed(xs) {
    var acc: int = 0
    for x in xs { acc = acc + check(x) }
    acc
}
pn arr_store(v) {
    var a: int[] = [0, 0, 0]
    a[1] = check(v)
    a
}
// a defect deep in a recursion reaches the outermost caller
fn depth(n: int, s) int { if (n == 0) check(s) else depth(n - 1, s) + 1 }
// a member or element of a defect-capable result is one too (S7.9.3)
type P = {n: int, s: string}
fn make(x) P {
    let k: int = x
    {n: k + 1, s: "ok"}
}
fn member_n(x) { make(x).n + 1 }
fn member_s(x) { len(make(x).s) }
// value-family system functions pass it through; integer arithmetic and
// equality over it stay exact
fn via_abs(x) { abs(check(x)) * 2 }
fn via_float(x) { float(check(x)) / 2.0 }
fn chain(x) { int((tchk(x) % 10) + 1) }
fn mix(x, y) { tchk(x) * tchk(y) - 3 }

pn main() {
    // operands and accumulators
    print([check(3) + 1, check("abc") + 1])
    print("\n")
    print([sum_twice(2), sum_twice("q")])
    print("\n")
    print([acc_untyped([1, 2, 3]), acc_untyped([1, "a", 3]), acc_typed([1, "a", 3])])
    print("\n")
    // literal slots keep the error whole
    print([{v: check("abc")}, {v: tchk("abc")}])
    print("\n")
    print([[check("abc"), 2], [tchk("abc"), 2]])
    print("\n")
    // stores
    var values = fill(4, 1.25)
    let ok = guarded_store(values, 1)
    let bad = guarded_store(values, 10)
    print([ok, bad, bad is error])
    print("\n")
    print([arr_store(4), arr_store("z")])
    print("\n")
    // recursion
    print([depth(3, 1), depth(3, "r")])
    print("\n")
    // an error-admitting binding receives the error as its value (S7.8.1)
    let kept: int | error = check("k")
    print([kept is error, check(9)])
    print("\n")
    // members, value-family calls, arithmetic and equality
    let m = make("z")
    print([member_n(3), member_n("q"), member_s(3), member_s("q"), m is error, m.n is error])
    print("\n")
    print([via_abs(-5), via_abs("b"), via_float(3), via_float("c")])
    print("\n")
    print([chain(3), chain("a"), mix(2, 3), mix("p", 3), (mix("p", "q")).message])
    print("\n")
    let r = tchk(4)
    print([tchk(1) == 2, tchk("a") == 2, tchk("a") != 2, r == 5, r != 5, chain(8) == 10])
    print("\n")
}
