// Result37 (paraffins 2.35x, T21-2a window): subscript keys typed through a
// five-hop local chain and through a raw-int64 sys func keep the array witness
pn count(rcount, n) {
    var m = n - 1
    var half = shr(m, 1)
    var total = 0
    var nc1 = 0
    while (nc1 * 2 <= m) {
        var remain = m - nc1
        var nc2 = remain - nc1
        if (nc2 >= 0 and nc2 <= half) { total = total + rcount[nc1] * rcount[nc2] + rcount[half] }
        nc1 = nc1 + 1
    }
    return total
}
pn main() {
    var rcount = fill(12, 0)
    rcount[0] = 1
    var k = 1
    while (k < 12) { rcount[k] = k * 2; k = k + 1 }
    print(count(rcount, 11), "\n")
}
