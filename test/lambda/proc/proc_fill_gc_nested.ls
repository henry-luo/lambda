pn null16() { return fill(16, null) }
pn null32() { return fill(32, null) }

pn arr_new() {
    return { l0: null16() }
}

pn arr_get(a, idx) {
    var i2 = idx % 32
    var mid = shr(idx, 5)
    var i1 = mid % 16
    var i0 = shr(mid, 4)
    var l0 = a.l0
    var c1 = l0[i0]
    if (c1 == null) { return null }
    var c2 = c1[i1]
    if (c2 == null) { return null }
    return c2[i2]
}

pn arr_set(var a, idx, val) {
    var i2 = idx % 32
    var mid = shr(idx, 5)
    var i1 = mid % 16
    var i0 = shr(mid, 4)
    var l0 = a.l0
    var c1 = l0[i0]
    if (c1 == null) { c1 = null16()  l0[i0] = c1 }
    var c2 = c1[i1]
    if (c2 == null) { c2 = null32()  c1[i1] = c2 }
    c2[i2] = val
    return 0
}

pn main() {
    var a = arr_new()
    arr_set(a, 0, 123)
    var churn = fill(500000, 0)
    var n = arr_get(a, 0)
    print("gc nested: " ++ (int(n)) ++ " churn=" ++ (len(churn)) ++ "\n")
}
