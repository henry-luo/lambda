// Tune31 T31-3 / S5.1.1, S6.1.2: a later write invalidates the stable
// Bool-or-null Item proof, so equality remains on its generic semantic path.

pn compare_after_write(values, index) {
    var left = values[index] > 0
    left = true
    var right = values[index + 1] > 0
    print((left != right) ++ "\n")
}

pn main() {
    compare_after_write([1, -1], 0)
    compare_after_write([1], 0)
}
