// S7.10.5v3/D2.2.2: abs retains finite, poison and absent integer lanes.
pn magnitude(values: int[], index: int) { return abs(values[index]) }
pn main() {
    var values: int[] = fill(6, 0)
    values[0] = -9007199254740991
    values[1] = 9007199254740991
    values[2] = int(1.0 / 0.0)
    values[3] = int(-1.0 / 0.0)
    values[4] = int(0.0 / 0.0)
    print([magnitude(values, 0), magnitude(values, 1), magnitude(values, 2),
        magnitude(values, 3), magnitude(values, 4), magnitude(values, 5),
        magnitude(values, 6), magnitude(values, -1)])
}
