// Tune4 M2 fixture: a closed, uniformly typed caller set may narrow parameters,
// while a function that escapes as a value must retain generic arithmetic.

pn closed_gap(x, y) {
    var count = 0
    while (x >= y) {
        x = x - y
        count = count + 1
    }
    return count
}

pn open_gap(x, y) {
    return x - y
}

pn apply2(f, a, b) {
    return f(a, b)
}

pn main() {
    print(string(closed_gap(17, 5)) ++ "\n")
    print(string(open_gap(10, 4)) ++ "\n")
    print(string(apply2(open_gap, 10.5, 4.25)) ++ "\n")
}
