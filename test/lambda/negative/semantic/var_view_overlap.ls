// Negative test: CW31/S9.2.4 exclusivity face 4 -- two mutable views of ONE
// base passed to two `var` parameters hand the callee two writers over one
// storage. Rejected at whole-base granularity (E211), same as face 3.
// Expected error: E211 "overlaps another `var` parameter through their shared view base"

pn two(var a, var b) {
    a[0] = 111
    b[0] = 222
}

pn main() {
    var arr = [1, 2, 3, 4, 5]
    var v1 = subview(arr, 0, 3)
    var v2 = subview(arr, 2, 5)
    two(v1, v2)
    print(v1[0])
}
