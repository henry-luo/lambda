// A `for` over bool elements binds its variable in bool's native lane, where
// `false` must stay falsy: S3.1 puts `false` in the falsy set (null, false,
// error values, ""). The JIT bound each fetched element Item into that lane
// without unboxing it, so `if`, `not` and `where` tested the tag word and read
// every element as true -- in statement loops, comprehensions, group-by keys
// and join sources alike (LR07-21). The interpreter was right; the tiers must
// agree (S1.6).

fn dyn(v) => v
fn pick(ys: bool[]) => [for (y in ys) (if (y) 1 else 0)]
fn pick_any(ys) => [for (y in ys) (if (y) 1 else 0)]

pn main() {
    let xs = [true, false]

    // statement loops: if/else, `not`, an if-expression and a counter
    for (b in xs) {
        if (b) { print("T") } else { print("F") }
    }
    for (b in xs) { print(if (not b) "n" else "-") }
    var hits = 0
    for (b in [false, true, false, true, true]) {
        if (b) { hits = hits + 1 }
    }
    print(" ")
    print(hits)
    print("\n")

    // elements computed at run time, a fill, and a `where` clause
    let flags = [dyn(1) > 2, dyn(2) > 1]
    for (b in flags) { print(if (b) "t" else "f") }
    for (b in fill(2, false)) { print(if (b) "t" else "f") }
    for (b in xs where not b) { print("w") }
    print("\n")

    // a mixed array binds the Item lane; an index read was already right
    for (b in [true, false, 1]) { print(if (b) "t" else "f") }
    print(if (xs[1]) " truthy" else " falsy")
    print("\n")

    // comprehension clauses: body, where, nested and indexed sources
    print([for (b in xs) (if (b) "T" else "F")])
    print([for (b in xs where b) "kept"])
    print([for (b in xs where not b) "negated"])
    print("\n")
    print([for (b in xs, c in xs) (if (b) "T" else "F") ++ (if (c) "T" else "F")])
    print([for (i, b in xs) (if (b) "T" else "F") ++ string(i)])
    print("\n")

    // group-by keys and join sources bind through the same path
    print([for (b in [true, false, false] group by (if (b) "yes" else "no") as k into g)
        [g.k, len(content(g))]])
    print("\n")
    print([for (a in xs, b in xs on a == b) (if (a) "A" else "a") ++ (if (b) "B" else "b")])
    print([for (a in xs, b? in [false] on a == b) (if (a) "A" else "a") ++ (if (b) "B" else "b")])
    print("\n")

    // typed and untyped parameters, map values, and boxing consumers
    print([pick([false, true, false]), pick_any([false, true, false])])
    print([for (k, v in {a: true, b: false}) (if (v) "on" else "off")])
    print([for (b in xs) (b and "and"), for (b in xs) (b or "or")])
    print("\n")
}
