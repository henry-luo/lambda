// D3.3.3v3/S9.2.2: a nested primitive contract may use one owned N-D
// ArrayNum lane; chained writes address that lane without materializing rows.
pn set_int_cell(var matrix: int[][], row: int, column: int, value: int) {
    matrix[row][column] = value
}

pn set_dynamic_cell(var matrix: int[][], row: int, column: int, value: any) {
    matrix[row][column] = value
}

pn main() {
    var matrix: int[][] = [[1, 2], [3, 4]]
    let snapshot = matrix
    set_int_cell(matrix, 1, 0, 9)

    var rejected = false
    set_dynamic_cell(matrix, 0, 1, "bad") ^ { rejected = true }
    var out_of_bounds = false
    set_int_cell(matrix, 4, 0, 7) ^ { out_of_bounds = true }
    print([matrix[0][1], snapshot[1][0], matrix[1][0], rejected, out_of_bounds])
}
