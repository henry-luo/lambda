pn store(var matrix: int[][], row: int, column: int, value: int) {
    matrix[row][column] = value
}

pn main() {
    var matrix: int[][] = [[1, 2], [3, 4]]
    store(matrix, 1, 0, 9)
    print(matrix[1][0])
}
