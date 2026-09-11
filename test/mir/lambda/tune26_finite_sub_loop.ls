// Tune26 T26-3: a finite positive-subtraction loop proves its parameter
// lanes once, then uses native compare/subtract/add within the guarded body.

pn tune26_divide(x: int, y: int) int {
    var q: int = 0
    while (x >= y) {
        x = x - y
        q = q + 1
    }
    return q
}

pn main() {
    print(tune26_divide(20, 3)); print(" ")
    print(tune26_divide(8, 2)); print("\n")
}
