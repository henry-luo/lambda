// D5.3/D8.4.3v2: the same scalar hoister serves Lambda's native loop.
pn stable_abs(n: int, value: float) float {
    var total = 0.0
    var i = 0
    while (i < n) {
        total = total + abs(value)
        i = i + 1
    }
    return total
}
pn main() {
    print([stable_abs(0, -2.5), stable_abs(4, -2.5)])
}
