pn touch(var values: float[]) {
    values[0] = values[0] + 1.0
}

pn calculate(var values: float[], source: float[]) {
    touch(values)
    var i: int = 0
    while (i < len(values)) {
        values[i] = source[i] * 2.0 + values[i]
        i = i + 1
    }
}

pn main() {
    var values: float[] = [1.0, 2.0, 3.0]
    let before = values
    let source: float[] = [4.0, 5.0, 6.0]
    calculate(values, source)
    print([before, values, source])
}
