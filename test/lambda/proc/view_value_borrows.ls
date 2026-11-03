// S9.2.2v2: subview values have independent storage and may be borrowed together.
pn two(var a, var b) {
    a[0] = 111
    b[0] = 222
}

pn main() {
    var arr = [1, 2, 3, 4, 5]
    var v1 = subview(arr, 0, 3)
    var v2 = subview(arr, 2, 5)
    two(v1, v2)^
    print([arr, v1, v2]); print("\n")
}
