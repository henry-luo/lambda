// S9.1.2: a parameter's value belongs to its caller. `var r = o` (or `r = o`)
// aliased it without marking either binding, so a write through `r` changed
// the caller's container -- even a `let` binding -- on every tier.

pn by_member(o) {
    var r = o
    r.x = 7
    r
}

pn by_index(o, k: string) {
    var r = o
    r[k] = 9
    r
}

pn by_reassign(o) {
    var r = {x: 0}
    r = o
    r.x = 5
    r
}

pn main() {
    let a = {x: 1}
    let b = by_member(a)
    print("member: " ++ format(a, 'json') ++ " -> " ++ format(b, 'json') ++ "\n")
    let c = {x: 1}
    let d = by_index(c, "x")
    print("index: " ++ format(c, 'json') ++ " -> " ++ format(d, 'json') ++ "\n")
    let e = {x: 1}
    let g = by_reassign(e)
    print("reassign: " ++ format(e, 'json') ++ " -> " ++ format(g, 'json') ++ "\n")
}
