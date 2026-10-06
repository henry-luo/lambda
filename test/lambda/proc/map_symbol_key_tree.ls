// S8.2.2v4 / D3.4.4v4: a symbol key and a string key with the same characters
// are one key, and a map grown by symbol keys -- a subscript write or a spread
// -- shares the runtime tree's edges with string-key growth instead of copying
// its whole shape on every add (LR03-38's residue for spreads).

pn fill_sym(var r: map, n: int, base: int) {
    var i: int = 0
    while (i < n) {
        r[symbol("k" ++ string(i))] = i + base
        i = i + 1
    }
}

pn fill_str(var r: map, n: int, base: int) {
    var i: int = 0
    while (i < n) {
        r["k" ++ string(i)] = i + base
        i = i + 1
    }
}

pn main() {
    var a = {}
    fill_sym(a, 4, 0)
    var b = {}
    fill_str(b, 4, 10)
    print("sym: " ++ format(a, 'json') ++ "\n")
    print("str: " ++ format(b, 'json') ++ "\n")

    // one key whatever the spelling: no second field is added
    a["k1"] = -1
    a['k2'] = -2
    print("mixed: " ++ format(a, 'json') ++ " len=" ++ string(len(a)) ++
        " k1=" ++ string(a['k1']) ++ " k2=" ++ string(a["k2"]) ++ "\n")

    // a spread adds the source's keys in order, and a later key overrides one
    let base = {x: 1, y: 2, z: 3}
    let s = {*:base, y: 20, w: 4}
    print("spread: " ++ format(s, 'json') ++ " len=" ++ string(len(s)) ++ "\n")
    var keys = ""
    for (k, v at s) { keys = keys ++ string(k) ++ " " }
    print("order: " ++ keys ++ "\n")

    // a null over a container field of a shared tree type reads back as the
    // canonical null, which the JIT compares by its bits (LR03-41)
    let with_list = {a: 1, marks: ['x', 'y']}
    let cleared = {*:with_list, marks: null}
    var grown = {}
    grown["a"] = 1
    grown["marks"] = ['z']
    grown["marks"] = null
    print("null over list: " ++ string(cleared.marks == null) ++ " " ++
        string(grown.marks == null) ++ " " ++ string(type(cleared.marks)) ++ "\n")

    // a wide spread, built twice from one source; the source is unchanged
    var wide = {}
    fill_str(wide, 300, 0)
    let c1 = {*:wide, extra: 1}
    let c2 = {*:wide, extra: 2}
    print("wide: len=" ++ string(len(c1)) ++ " k299=" ++ string(c1["k299"]) ++
        " c1.extra=" ++ string(c1.extra) ++ " c2.extra=" ++ string(c2.extra) ++
        " source=" ++ string(len(wide)) ++ "\n")
}
