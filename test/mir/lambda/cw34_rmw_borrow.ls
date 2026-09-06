// CW34: the read-modify-write handle idiom binds through cow_bind_rmw_handle
// (borrow when the spine is unshared), never through the snapshot bind, and
// its store-back emits no capture mark.
pn arr_set(var a, idx, val) {
    var i1 = idx % 16
    var i0 = idx div 16
    var l0 = a.l0
    var c1 = l0[i0]
    if (c1 == null) { c1 = fill(16, null) }
    c1[i1] = val
    l0[i0] = c1
    a.l0 = l0
    return 0
}
pn main() {
    var a = { l0: fill(4, null) }
    arr_set(a, 5, 42)
    arr_set(a, 21, 7)
    print(a.l0[0][5], " ", a.l0[1][5], "\n")
}
