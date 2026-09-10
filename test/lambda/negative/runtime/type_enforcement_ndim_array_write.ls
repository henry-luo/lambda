fn dynamic(value) => value

pn main() {
    var matrix: int[] = reshape([1, 2, 3, 4], [2, 2])
    matrix[1, 0] = dynamic("not an integer")
}
