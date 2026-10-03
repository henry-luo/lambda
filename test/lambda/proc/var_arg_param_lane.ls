// An untyped parameter the body uses arithmetically AND passes to an untyped
// `var` parameter must stay a rooted boxed binding, as a `var` local does
// (T21-3b): its CW33 home is how the callee's write reaches it. With an
// inferred native lane the JIT lost every write (`int_param` gave 160).
pn bump(var n) { n = n + 1 }
pn scale(var v) { v = v * 2.0 }
pn int_param(x) {
    var i = 0
    while (i < 6) { x = x + 10; bump(x); i = i + 1 }
    return x
}
pn float_param(y) {
    var i = 0
    while (i < 3) { y = y + 0.5; scale(y); i = i + 1 }
    return y
}
pn main() {
    var k = 0
    var a = 0
    var b = 0.0
    // repeated calls also exercise the promoted (satellite) callers
    while (k < 7) { a = int_param(100); b = float_param(1.0); k = k + 1 }
    print(a, " ", b, "\n")
}
