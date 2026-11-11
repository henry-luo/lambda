// S9.1.3: a procedure with `var` parameters may also take optional parameters. An omitted
// optional takes its default in the callee; the `var` argument is still a caller place (T0 and MIR).
pn bump(var m, by = 1) {
    m.n = m.n + by
}

pn grow(var m, extra = {k: 1}) {
    m.items = [*m.items, extra.k]
}

pn main() {
    var m = {n: 0, items: []}
    bump(m, 2)
    bump(m)
    bump(by: 5, m: m)
    grow(m)
    grow(m, {k: 7})
    var d = {inner: {n: 10, items: []}}
    bump(d.inner)^
    bump(d.inner, 4)^
    return {n: m.n, items: m.items, inner: d.inner.n}
}
