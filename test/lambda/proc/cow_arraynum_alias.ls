// CW32v2/S9.1.2 on ArrayNum: binding aliases are O(1) mark-and-share; the
// first write through EITHER handle detaches (one packed memcpy) and the other
// keeps its snapshot. Mutable views keep write-through (open/todo). Unaliased
// arrays keep fully raw lane stores.
pn main() {
    // S9.1.2 on ArrayNum: binding copies; writer detaches, snapshot holds
    var a = [1, 2, 3]
    var b = a
    b[0] = 99
    print(a[0]); print(" "); print(b[0]); print(" (ruled 1 99)\n")

    // the reverse direction: writing the ORIGINAL leaves the alias's snapshot
    var c = [4, 5, 6]
    var d = c
    c[1] = 77
    print(c[1]); print(" "); print(d[1]); print(" (ruled 77 5)\n")

    // float lane
    var e = [1.5, 2.5]
    var g = e
    g[0] = 9.5
    print(e[0]); print(" "); print(g[0]); print(" (ruled 1.5 9.5)\n")

    // let alias: the classic false-unique canary
    let h = [7, 8, 9]
    var k = h
    k[2] = 0
    print(h[2]); print(" "); print(k[2]); print(" (ruled 9 0)\n")

    // subview write-through unchanged (mutable views open/todo)
    var m = [10, 20, 30, 40]
    var v = subview(m, 1, 3)
    v[0] = 999
    print(m[1]); print(" (ruled 999: view writes through)\n")

    // unaliased hot loop: pure raw stores, correctness unchanged
    var s = [0, 0, 0, 0]
    for (i in 0 to 3) { s[i] = i * i }
    print(s[3]); print(" (ruled 9)\n")

    // mask store on an aliased array
    var p = [1, 2, 3, 4]
    var q = p
    p[p gt 2] = 0
    print(p[2]); print(" "); print(q[2]); print(" (ruled 0 3)\n")
}
