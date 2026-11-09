// S9.1.2 / D4.3.1: closed mutable parameter lanes retain value copies.
pn visit(values, depth) {
    if (depth == 0) { return values[0] }
    values[0] = depth
    let below = visit(values, depth - 1)
    return below + values[0]
}
pn replace(values, depth, replacement) {
    if (depth == 0) { return values[0] }
    values[0] = replacement
    return replace(values, depth - 1, replacement)
}
pn main() {
    var data = [99]
    print([visit(data, 3), data]); print("\n")
    print([replace(data, 2, "changed"), data]); print("\n")
    print([replace(data, 1, null), data]); print("\n")
}
