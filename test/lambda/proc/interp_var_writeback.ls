// T21-3b (D8.1.1v6): `var` parameters and indexed/member stores across the
// tier boundary. Under the AUTO default every function here promotes to a
// satellite after its fifth entry (each loop runs six), so the tail of each
// loop exercises: T0 -> satellite with a var scalar rebind and a var array
// (in-place write, alias `b` untouched); satellite -> T0 (`inner` stays
// interpreted because of its match expression) with a var array; a
// satellite -> satellite var chain; and an indexed store through a plain
// parameter that must keep value semantics. The golden must match on the
// interpreter and the JIT as well.
// (a) T0 -> satellite: var scalar rebind and var array in-place + detach
pn bump(var n) { n = n + 1 }
pn setit(var a, i, v) { a[i] = v }
// (c) satellite -> T0 callee with a var param (pinned by the match expression)
pn inner(var v, x) { v[0] = match x { case 1: 100  default: 200 } }
pn outer(var v, x) { inner(v, x); v[1] = v[1] + 1 }
// (d) satellite -> satellite var chain (quicksort shape)
pn swap(var arr, i, j) { var t = arr[i]; arr[i] = arr[j]; arr[j] = t }
pn rev(var arr, lo, hi) { if (lo < hi) { swap(arr, lo, hi); rev(arr, lo + 1, hi - 1) } }
// (e) indexed stores through a plain parameter keep value semantics
pn touch(a) { a[0] = 5; a[0] }
pn main() {
    var n = 0
    var k = 0
    while (k < 6) { bump(n); k = k + 1 }
    print(n); print(" ")
    var a = [1, 2, 3]
    let b = a
    k = 0
    while (k < 6) { setit(a, 0, 9 + k); k = k + 1 }
    print(a); print(b); print(" ")
    var v = [0, 0]
    k = 0
    while (k < 6) { outer(v, k); k = k + 1 }
    print(v); print(" ")
    var r = [1, 2, 3, 4, 5]
    k = 0
    while (k < 6) { rev(r, 0, 4); k = k + 1 }
    print(r); print(" ")
    var c = [7, 8]
    k = 0
    while (k < 6) { touch(c); k = k + 1 }
    print(c); print(touch(c)); print("\n")
}
