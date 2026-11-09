// S9.1.2/S9.2.2: every row value is independent of its source matrix.
fn keep(row) { () => row }
fn identity(row) => row
pn write(var row) { row[0] = 7 }
pn main() {
    var m = [[1, 2, 3], [4, 5, 6]]
    var row = m[0]
    let captured = keep(m[0])
    let held = {row: m[0]}
    var returned = identity(m[0])
    var reassigned = [0, 0, 0]
    reassigned = m[0]
    m[0][1] = 9
    row[2] = 8
    returned[0] = 6
    reassigned[0] = 5
    print([m, row, captured(), held.row, returned, reassigned]); print("\n")
    write(m[1])^
    print(m); print("\n")
    var source = [10, 20, 30, 40]
    var slice_value = subview(source, 1, 3)
    source[1] = 90
    slice_value[1] = 80
    print([source, slice_value]); print("\n")
    var transposed = transpose(m)
    transposed[0][0] = 99
    print([m, transposed]); print("\n")
}
