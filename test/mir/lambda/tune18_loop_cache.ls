// Tune18 E2.b: an inferred numeric-array witness is admitted once before a
// repeated native call edge, rather than re-proved inside the loop.

pn sample18(values) float {
    return values[0] + 1.0
}

pn main() {
    var values = [1.0, 2.0]
    var total: float = 0.0
    var i: int = 0
    while (i < 3) {
        total = total + sample18(values)
        i = i + 1
    }
    print(total)
}
