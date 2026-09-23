fn dynamic(value) => value

pn main() {
    // a 2-D reshape is `int[][]` (S11.1.1v3); the write is what must fail
    var matrix: int[][] = reshape([1, 2, 3, 4], [2, 2])
    matrix[1, 0] = dynamic("not an integer")
}
