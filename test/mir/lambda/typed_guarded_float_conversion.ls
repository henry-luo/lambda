// S4.5.3/S7.7.2: native successful conversion retains the cold error join.
pn typed(values: int[], index: int) {
    var value: float = float(values[index]) / 2.0
    return value
}

pn inferred(values: int[], index: int) {
    var value = float(values[index]) / 2.0
    return value
}

pn mixed(values: int[], other: float[], index: int) {
    return float(values[index]) + other[index]
}

pn mixed_decl(values: int[], other: float[], index: int) {
    var value: float = float(values[index]) + other[index]
    return value
}

pn scalar_conversion(values: float[], index: int) {
    var value: float = float(index) - values[index]
    value = float(index) - values[index]
    return value
}

pn main() {
    print([typed([5], 0), inferred([5], 0), mixed([5], [1.5], 0)])
    print([type(typed([5], 1)), type(inferred([5], -1))])
    var empty_float: float[] = fill(0, 0.0)
    var empty_int: int[] = fill(0, 0)
    print([type(mixed([5], empty_float, 0)), type(mixed(empty_int, [1.5], 0))])
    var poison: int[] = fill(3, 0)
    poison[0] = int(1.0 / 0.0)
    poison[1] = int(-1.0 / 0.0)
    poison[2] = int(0.0 / 0.0)
    print([typed(poison, 0), typed(poison, 1), typed(poison, 2)])
    print([scalar_conversion([1.5], 0), type(scalar_conversion(empty_float, 0)),
        mixed_decl([5], [1.5], 0), type(mixed_decl([5], empty_float, 0)),
        type(mixed_decl(empty_int, [1.5], 0))])
}
