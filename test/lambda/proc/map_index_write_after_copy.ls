// S9.1.2: an index write to a map binding that has been copied must land in
// that binding. The member lowering called the COW setter, which detaches a
// shared owner and returns the private copy, but never republished the copy:
// after `var h = g`, `g["k0"] = 99` left g unchanged.
pn main() {
    var g = {k0: 0, k1: 1}
    var h = g
    g["k0"] = 99
    print("const key: " ++ format(g, 'json') ++ " copy " ++ format(h, 'json') ++ "\n")
    var g2 = {k0: 0, k1: 1}
    var h2 = g2
    h2["k1"] = 5
    let key = "k0"
    g2[key] = 99
    print("var key: " ++ format(g2, 'json') ++ " copy " ++ format(h2, 'json') ++ "\n")
    var outer = {inner: {a: 1}}
    var snapshot = outer
    outer.inner["a"] = 2
    print("nested: " ++ format(outer, 'json') ++ " copy " ++ format(snapshot, 'json') ++ "\n")
}
