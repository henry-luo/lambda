// D8.1.1v9: a satellite image co-compiles its target's direct-callee cluster,
// so calls among them are direct native edges with the eager tier's call-site
// inference (`half`/`sub_until` take int lanes from their call sites), while a
// callee that must stay in T0 (a nested definition pins it) is still reached
// through the boxed dynamic edge, and a member promoted by an EARLIER image
// keeps its own entry. Golden is tier-agreed.
pn sub_until(x, y) {
    var q = 0
    while (x >= y) { x = x - y; q = q + 1 }
    return q
}
pn half(n) { return sub_until(n, 2) }
pn even_odd(n) { if (n == 0) { return "even" } else { return odd_even(n - 1) } }
pn odd_even(n) { if (n == 0) { return "odd" } else { return even_odd(n - 1) } }
pn pinned(n) { let f = (x) => x + 1; return f(n) }
pn bump(var n) { n = n + 1 }
pn warm(n) {
    var i = 0
    var acc = 0
    while (i < n) { acc = acc + half(i); i = i + 1 }
    return acc
}
pn main() {
    print(warm(10), "\n")
    var fs: float[] = fill(3, 0.5)
    var count = 0
    var k = 0
    while (k < 8) {
        count = count + sub_until(1000, 7) + half(k * 3)
        bump(count)
        fs[k % 3] = float(pinned(k))
        k = k + 1
    }
    print(count, " ", fs, " ", even_odd(7), " ", odd_even(6), "\n")
}
