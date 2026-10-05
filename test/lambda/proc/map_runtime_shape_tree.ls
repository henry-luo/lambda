// D3.4.3v4: plain maps grown at runtime share their types through the
// context's transition tree. Each new key used to copy the whole shape into a
// never-reclaimed pool (LR03-38), so building an n-key map cost O(n²) entries.
// Maps that share a tree type must still mutate independently.

pn fill(var r: map, n: int, tag: int) {
    var i: int = 0
    while (i < n) {
        r["k" ++ string(i)] = i + tag
        i = i + 1
    }
}

pn main() {
    // same sequence of adds: one shared type, independent values
    var a = {}
    fill(a, 3, 0)
    var b = {}
    fill(b, 3, 10)
    print("shared: " ++ format(a, 'json') ++ " " ++ format(b, 'json') ++ "\n")

    // a retype on one map leaves its siblings on the shared type
    var c = {}
    fill(c, 3, 0)
    c["k1"] = "text"
    var d = {}
    fill(d, 3, 0)
    print("retype: " ++ format(c, 'json') ++ " " ++ format(d, 'json') ++ "\n")

    // a null field takes a value, then a sibling grows past it
    var e = {}
    e["x"] = null
    e["y"] = 2
    var f = {}
    f["x"] = null
    f["y"] = 3
    e["x"] = [1, 2]
    f["z"] = "added"
    print("null lane: " ++ format(e, 'json') ++ " " ++ format(f, 'json') ++ "\n")

    // a copy grows apart from its source
    var g = {}
    fill(g, 2, 0)
    var h = g
    h["extra"] = true
    g["k0"] = 99
    print("copy: " ++ format(g, 'json') ++ " " ++ format(h, 'json') ++ "\n")

    // insertion order, overwrite and lookups across many keys
    var big = {}
    fill(big, 300, 0)
    big["k150"] = -1
    var sum = 0
    for (k, v at big) { sum = sum + v }
    print("big: len=" ++ string(len(big)) ++ " k0=" ++ string(big["k0"]) ++
        " k150=" ++ string(big["k150"]) ++ " k299=" ++ string(big["k299"]) ++
        " sum=" ++ string(sum) ++ "\n")
    var small = {}
    fill(small, 4, 0)
    var keys = ""
    for (k, v at small) { keys = keys ++ string(k) ++ " " }
    print("order: " ++ keys ++ "\n")
}
