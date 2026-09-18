// LR12-11 (S9.1.2, S9.1.3): a call result that is (part of) an argument is a
// second observer of the caller's value. Binding it copies observably: a write
// on either side after the call must not reach the other. Both tiers used to
// treat every call result as a fresh owner, so each line below printed 9.
type Box = {size: int, tag: int}
type Holder = {items: Box[], n: int}
type Pair = {inner: Box, n: int}

pn keep(p: Box) Box { return p }
pn keep_if(p: Box, q: Box, c: bool) Box { return if (c) p else q }
pn kid(h: Holder) Box { return h.items[0] }
pn keep_let(p: Box) Box {
    let t = p
    return t
}
fn keepf(p: Box) Box => p
pn untyped_keep(p) { return p }
// the alias passes through a callee defined later, and through recursion
pn wrapper(p: Box) Box { return later(p) }
pn later(p: Box) Box { return p }
pn rec(p: Box, n: int) Box {
    if (n == 0) { return p }
    return rec(p, n - 1)
}
pn pick(xs: Box[]) Box {
    for x in xs { return x }
    return {size: 0, tag: 0}
}
// a `var` parameter keeps writing through; the call site marks the result
pn keepv(var b: Box) Box { return b }
pn keepv_tail(var b: Box) Box { b }
pn bump(var b: Box) Box {
    b.size = 5
    return b
}

pn source_write() {
    var b: Box = {size: 1, tag: 0}
    var r = keep(b)
    b.size = 9
    print("keep=" ++ r.size ++ "\n")
    var b2: Box = {size: 1, tag: 0}
    var o: Box = {size: 2, tag: 0}
    var r2 = keep_if(b2, o, true)
    b2.size = 9
    print("keep_if=" ++ r2.size ++ "\n")
    var h: Holder = {items: [{size: 1, tag: 0}], n: 0}
    var r3 = kid(h)
    h.items[0].size = 9
    print("kid=" ++ r3.size ++ "\n")
    var b4: Box = {size: 1, tag: 0}
    var r4 = keep_let(b4)
    b4.size = 9
    print("keep_let=" ++ r4.size ++ "\n")
    var b5: Box = {size: 1, tag: 0}
    var r5 = keepf(b5)
    b5.size = 9
    print("keepf=" ++ r5.size ++ "\n")
    var b6 = {size: 1}
    var r6 = untyped_keep(b6)
    b6.size = 9
    print("untyped=" ++ r6.size ++ "\n")
    var b7: Box = {size: 1, tag: 0}
    var r7 = rec(b7, 3)
    b7.size = 9
    print("rec=" ++ r7.size ++ "\n")
    var xs: Box[] = [{size: 1, tag: 0}]
    var r8 = pick(xs)
    xs[0].size = 9
    print("pick=" ++ r8.size ++ "\n")
}

pn result_write() {
    var b: Box = {size: 1, tag: 0}
    var r = keep(b)
    r.size = 9
    print("write_result=" ++ b.size ++ "\n")
    var b2: Box = {size: 1, tag: 0}
    var r2 = wrapper(b2)
    r2.size = 9
    print("wrapper=" ++ b2.size ++ "\n")
    var b3: Box = {size: 1, tag: 0}
    var r3: Box = keep(b3)
    r3.size = 9
    print("typed_result=" ++ b3.size ++ " r=" ++ r3.size ++ "\n")
    var b4: Box = {size: 1, tag: 0}
    var r4: Box = {size: 0, tag: 0}
    r4 = keep(b4)
    r4.size = 9
    print("reassign=" ++ b4.size ++ "\n")
}

pn var_results() {
    var b: Box = {size: 1, tag: 0}
    var r = keepv(b)
    b.size = 9
    print("keepv=" ++ r.size ++ "\n")
    var b2: Box = {size: 1, tag: 0}
    var r2 = keepv_tail(b2)
    r2.size = 9
    print("keepv_tail=" ++ b2.size ++ "\n")
    var b3: Box = {size: 1, tag: 0}
    var r3 = bump(b3)
    b3.size = 9
    print("bump=" ++ r3.size ++ " b=" ++ b3.size ++ "\n")
    var p: Pair = {inner: {size: 1, tag: 0}, n: 0}
    var r4 = keepv(p.inner)
    p.inner.size = 9
    print("place=" ++ r4.size ++ " p=" ++ p.inner.size ++ "\n")
    var p2: Pair = {inner: {size: 1, tag: 0}, n: 0}
    var r5 = keepv(p2.inner)
    r5.size = 9
    print("place_write_result=" ++ p2.inner.size ++ "\n")
}

pn plain_param_call(q: Box) int {
    var r = keep(q)
    q.size = 9
    return r.size * 100 + q.size
}

pn main() {
    source_write()
    result_write()
    var_results()
    var c: Box = {size: 1, tag: 0}
    print("plain=" ++ plain_param_call(c) ++ " caller=" ++ c.size ++ "\n")
}
