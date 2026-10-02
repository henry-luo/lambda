// D8.3.2: only a matching scalar consumer justifies inferred forwarding.
pn tune32_scalar_pick(a, b) {
    if (a < b) { return a }
    return b
}
pn tune32_scalar_forward(a, b, c) {
    return tune32_scalar_pick(tune32_scalar_pick(a, b), c)
}
pn main() {
    print(tune32_scalar_forward(7, 3, 9)); print("\n")
    let escaped = tune32_scalar_forward
    print(escaped(1.5, 2.5, 0.5)); print("\n")
    print(escaped("z", "a", "b")); print("\n")
    print(escaped(null, 2, 3)); print("\n")
    var rows = fill(3, 0)
    var i = 0
    while (i < 3) {
        rows[i] = tune32_scalar_forward(i + 3, i + 1, i + 2)
        i = i + 1
    }
    var other = fill(3, 7)
    var saved = rows
    var temporary = rows
    rows = other
    other = temporary
    rows[0] = 9
    print(rows); print(" "); print(other); print(" "); print(saved); print("\n")
}
