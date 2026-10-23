// S4.1/S6.1.2: inductive content facts never erase signed/absent/escape paths.
pn modular(steps: int) {
    var values = fill(4, 0)
    var i = 0
    while (i < steps) { values[0] = (values[0] + 3) % 16; i = i + 1 }
    return values[0]
}
pn signed(steps: int) {
    var values = fill(4, -1)
    var i = 0
    while (i < steps) { values[0] = (values[0] - 3) % 16; i = i + 1 }
    return values[0]
}
pn retained(steps: int) {
    var values = fill(4, 0)
    let old = values
    var i = 0
    while (i < steps) { values[0] = (values[0] + 3) % 16; i = i + 1 }
    return [old[0], values[0]]
}
pn set_negative(var values) { values[0] = -5 }
pn borrowed() {
    var values = fill(4, 0)
    set_negative(values)
    values[0] = (values[0] + 1) % 16
    return values[0]
}
pn absent() {
    var values = fill(4, 0)
    values[0] = (values[-1] + 1) % 16
    return values[0]
}
pn main() { print([modular(11), signed(5), retained(11), borrowed(), absent()]); print("\n") }
