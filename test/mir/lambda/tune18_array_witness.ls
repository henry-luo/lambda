// Tune18 E2.b: a stable local typed-array witness crosses repeated call edges.

pn take18(values: int[]) int {
    return values[0]
}

pn main() {
    var values: int[] = fill(1, 4)
    var i: int = 0
    var total: int = 0
    while (i < 3) {
        total = total + take18(values)
        i = i + 1
    }
    print(total)
}
