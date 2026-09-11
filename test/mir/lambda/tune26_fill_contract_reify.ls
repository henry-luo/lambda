// Tune26: a fresh numeric fill satisfies an annotated array contract directly.
pn tune26_fill_contract_take(values: int[]) int {
    return len(values)
}

pn tune26_fill_contract_reify(n: int) int {
    var values: int[] = fill(n, 7)
    if (n == 0) {
        return tune26_fill_contract_take(values)
    }
    return values[0] + values[n - 1] + tune26_fill_contract_take(values)
}

// A nonempty float fill must carry its declaration certificate into a borrowed
// write; the raw ArrayNum alone is insufficient for an in-place `var` body.
pn tune26_fill_contract_mutate(var values: float[]) float {
    values[0] = values[0] + 1.0
    return values[0]
}

pn tune26_fill_contract_float_reify(n: int) float {
    var values: float[] = fill(n, 0.0)
    if (n == 0) {
        return float(len(values))
    }
    return tune26_fill_contract_mutate(values)
}

pn main() {
    print(tune26_fill_contract_reify(0) ++ " " ++ tune26_fill_contract_reify(2) ++
        " " ++ tune26_fill_contract_float_reify(2) ++ " " ++
        tune26_fill_contract_float_reify(0))
}
