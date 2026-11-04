// S4.1/D2.3.4/D6.2-D6.3: nullable native failures stay outside the hot path.
pn add_at(var values: float[], index: int) any {
    // S7.7.2v2/S7.6.3v3: admission retains its error; explicitly exit before arithmetic.
    let initial: float = values[index]
    var total: float = initial^
    let next: float = total + values[index]
    total = next^
    return total
}

pn main() {
    var values: float[] = [1.5, 2.5]
    print(add_at(values, 0))
    print("\n")
    print(add_at(values, 9) is error)
}
