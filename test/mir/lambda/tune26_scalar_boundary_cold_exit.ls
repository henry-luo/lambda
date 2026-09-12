// S4.1/D2.4.1/D5.2-D5.3: nullable native failures stay outside the hot path.
pn add_at(var values: float[], index: int) any {
    var total: float = values[index]
    total = total + values[index]
    return total
}

pn main() {
    var values: float[] = [1.5, 2.5]
    print(add_at(values, 0))
}
