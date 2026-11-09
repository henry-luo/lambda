// S9.2.2v2: ordinary view bindings snapshot; a place argument writes through.

pn test_subview_snapshot() {
    var arr = [10, 20, 30, 40, 50]
    var v = subview(arr, 1, 4)   // snapshots [20, 30, 40]
    v[0] = 999                   // changes only the snapshot
    print(v[0]); print(" "); print(arr[1]); print("\n")   // 999 20
    v[2] = 777                   // changes only the snapshot
    print(arr[3]); print("\n")   // 40
}

pn test_float_view_snapshot() {
    var arr = [1.5, 2.5, 3.5, 4.5, 5.5]
    var v = subview(arr, 0, 3)
    v[1] = 99.9
    print(arr[1]); print("\n")   // 2.5
}

// S9.2.2: a write-through view is a BORROW, never a value -- legal only in
// `var`-parameter position. Taking the row as a binding (`var row = m[1]`) binds
// a copy under S9.1.2, so this is the row write spelled as the borrow it is.
pn write_row(var row) {
    row[0] = 99                  // -> m[1][0]
    row[2] = 88                  // -> m[1][2]
}

pn test_leading_axis_row_write() {
    var m = [[1, 2, 3], [4, 5, 6]]
    write_row(m[1])^              // leading-axis row 1 borrowed -> [4, 5, 6]
    print(m[1][0]); print(" "); print(m[1][1]); print(" "); print(m[1][2]); print("\n")  // 99 5 88
}

pn test_compact_view_write() {
    var img = [10u8, 20u8, 30u8, 40u8, 50u8]
    var crop = subview(img, 1, 4)   // snapshots [20, 30, 40]
    crop[1] = 200                   // changes only the snapshot
    print(img[2]); print("\n")      // 30
}

pn main() {
    test_subview_snapshot()
    test_float_view_snapshot()
    test_leading_axis_row_write()^
    test_compact_view_write()
}
