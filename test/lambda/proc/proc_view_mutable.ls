// Mutable ArrayNum views — procedural write-through to the base (Typed Array 4, Scope 3).
// Inside a pn function, writing through a view (subview, leading-axis row) mutates
// the aliased base in place — no full copy.  Pure fn code cannot issue these writes
// (index assignment is a procedural-only statement), so the functional core stays
// value-semantic by construction.

pn test_subview_write_through() {
    var arr = [10, 20, 30, 40, 50]
    var v = subview(arr, 1, 4)   // aliases [20, 30, 40]
    v[0] = 999                   // writes through to arr[1]
    print(v[0]); print(" "); print(arr[1]); print("\n")   // 999 999
    v[2] = 777                   // writes through to arr[3]
    print(arr[3]); print("\n")   // 777
}

pn test_float_view_write_through() {
    var arr = [1.5, 2.5, 3.5, 4.5, 5.5]
    var v = subview(arr, 0, 3)
    v[1] = 99.9
    print(arr[1]); print("\n")   // 99.9
}

pn test_leading_axis_row_write() {
    var m = [[1, 2, 3], [4, 5, 6]]
    var row = m[1]               // leading-axis view of row 1 -> [4, 5, 6]
    row[0] = 99                  // -> m[1][0]
    row[2] = 88                  // -> m[1][2]
    print(m[1][0]); print(" "); print(m[1][1]); print(" "); print(m[1][2]); print("\n")  // 99 5 88
}

pn test_compact_view_write() {
    var img = [10u8, 20u8, 30u8, 40u8, 50u8]
    var crop = subview(img, 1, 4)   // aliases [20, 30, 40]
    crop[1] = 200                   // -> img[2]
    print(img[2]); print("\n")      // 200
}

pn main() {
    test_subview_write_through()
    test_float_view_write_through()
    test_leading_axis_row_write()
    test_compact_view_write()
}
