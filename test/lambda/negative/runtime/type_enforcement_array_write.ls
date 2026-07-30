fn identity(value) => value

pn main() {
    var values: int[] = [1, 2, 3]
    values[1] = identity("not an integer")
}
