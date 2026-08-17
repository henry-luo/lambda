// Tune18 E2.a: CP1 double literals stay MIR immediates inside a loop.

pn main() {
    var total: float = 0.0
    var i: int = 0
    while (i < 3) {
        total = total + 1.0
        i = i + 1
    }
    print(total)
}
