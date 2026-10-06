// D3.4.3v5: a map grows from whatever type it has -- a literal's, a parsed
// document's, a contract's, a nominal type's, a retyped map's, an element's --
// through the runtime tree, which keeps the edge from that external parent in
// its own table and never writes the parent. Each add used to copy the whole
// shape (LR03-38's residue). Values, order, record identity and copy-on-write
// must be exactly as before.

type R = {a: int, b: string}
type P { x: int, fn dbl() => x * 2 }

pn grow(var r: map, n: int, tag: int) {
    var i: int = 0
    while (i < n) {
        r["g" ++ string(i)] = i + tag
        i = i + 1
    }
}

pn keys_of(m) {
    var out = ""
    for (k, v at m) { out = out ++ string(k) ++ " " }
    out
}

pn main() {
    // a literal: two maps born from one literal grow alike; the literal's next
    // evaluation is untouched
    var l1 = {a: 1, b: "x"}
    var l2 = {a: 2, b: "y"}
    grow(l1, 3, 0)
    grow(l2, 3, 10)
    let l3 = {a: 3, b: "z"}
    print("literal: " ++ format(l1, 'json') ++ " " ++ format(l2, 'json') ++
        " fresh=" ++ string(len(l3)) ++ "\n")
    print("literal order: " ++ keys_of(l1) ++ "\n")

    // a parsed object: the copy grows, the document is unchanged
    let doc = parse("{\"a\": 1, \"b\": 2}", 'json')^
    var p1 = doc
    p1.c = 3
    p1["d"] = "four"
    print("parsed: " ++ format(p1, 'json') ++ " doc=" ++ string(len(doc)) ++
        " " ++ keys_of(p1) ++ "\n")

    // a contract record gains an open field and still conforms
    var r: R = {a: 1, b: "s"}
    r["extra"] = true
    print("contract: " ++ format(r, 'json') ++ " is R=" ++ string(r is R) ++
        " len=" ++ string(len(r)) ++ "\n")

    // a nominal instance keeps its type and methods through growth
    var n1 = <P x: 5>
    n1.z = 9
    n1.w = "w"
    var n2 = <P x: 6>
    n2.z = 1
    n2.w = "v"
    print("nominal: " ++ string(n1.dbl()) ++ " " ++ string(n2.dbl()) ++ " " ++
        string(n1 is P) ++ " " ++ string(n2 is P) ++ " " ++ string(len(n1)) ++
        " " ++ string(n1.z + n2.z) ++ "\n")

    // a retyped map grows from its rebuilt type
    var m = {a: 1, b: 2}
    m.a = "text"
    m.c = 3
    print("retyped: " ++ format(m, 'json') ++ " " ++ keys_of(m) ++ "\n")

    // an element grows its attributes and keeps its content
    var e = <div class: "box", "content">
    e.id = "main"
    e.n = 2
    print("element: " ++ string(e.class) ++ " " ++ string(e.id) ++ " " ++
        string(e.n) ++ " " ++ string(e[0]) ++ " " ++ string(len(e)) ++ "\n")

    // copy-on-write: the copy grows apart from its source
    var s1 = {k: 1}
    var s2 = s1
    s2.extra = 2
    s1.k = 10
    print("cow: " ++ format(s1, 'json') ++ " " ++ format(s2, 'json') ++ "\n")

    // the residue case: many keys from a literal start, read back
    var wide = {seed: 0}
    grow(wide, 300, 0)
    print("wide: len=" ++ string(len(wide)) ++ " g0=" ++ string(wide.g0) ++
        " g150=" ++ string(wide["g150"]) ++ " g299=" ++ string(wide.g299) ++
        " seed=" ++ string(wide.seed) ++ "\n")
}
