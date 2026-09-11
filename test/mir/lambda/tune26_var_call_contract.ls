// Tune26 T26-4: a direct var call with the same float[] contract may replace
// its COW owner, but the caller reloads it and keeps the proved packed lane.

pn tune26_var_call_boundary(var values: float[]) any {
    values[0] = values[0] + 1.0
}

pn tune26_var_call_contract(var values: float[]) float {
    tune26_var_call_boundary(values)
    return values[0] + values[1]
}

pn main() {
    var values: float[] = [1.0, 2.0]
    let snapshot = values
    print(tune26_var_call_contract(values)); print(" ")
    print(values[0]); print(" ")
    print(snapshot[0]); print("\n")
}
