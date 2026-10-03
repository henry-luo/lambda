// S6.1.2/D2.4.1-D2.4.3: a nullable comparison stays a 0/1/2 bool lane.
pn classify(values: int[], index: int) int {
    if (values[index] > 0) { return 1 }
    if (values[index] <= 0) { return -1 }
    return 0
}

pn compare(values: int[], index: int) {
    print(string([values[index] > 0, values[index] <= 0,
        values[index] > 0.0, values[index] <= 0.0,
        classify(values, index)]) ++ "\n")
}

pn read_arithmetic(values: int[], index: int) {
    let value: int = values[index] + 1
    return value
}

pn main() {
    compare([1, -1], 0)
    compare([1, -1], 1)
    compare([1, -1], 2)
    compare([1, -1], -1)
    print([read_arithmetic([1], 0), type(read_arithmetic([1], 1))])
    print("\n")
}
