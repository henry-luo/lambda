// D5.3, S9.1.3: reused roots stay precise; later argument calls capture the old value.
pn consume(values: float[], count: int, bias: float) float {
    let scratch = [for (i in 1 to 30) [i, i + 1]]
    var total: float = bias + float(len(scratch))
    var i: int = 0
    while (i < count) { total = total + values[i]; i = i + 1 }
    return total
}

pn replace_values(var values: float[]) float {
    values = [100.0, 200.0]
    return 1.0
}

pn defaulted(values: float[], count: int, bias: float = 2.0) float {
    return consume(values, count, bias)
}

pn main() {
    var values: float[] = [2.0, 3.0]
    var count: int = 2
    var bias: float = 0.5
    var total: float = 0.0
    var i: int = 0
    while (i < 3) { total = total + consume(values, count, bias); i = i + 1 }
    print([total, defaulted(values, count)]); print("\n")
    let before = values
    let captured = consume(values, count, replace_values(values))
    print([captured, before, values]); print("\n")
    print(type(consume(values, 3, bias))); print("\n")
}
