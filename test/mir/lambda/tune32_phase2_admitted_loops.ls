// D3.3.4 / S9.1.3: layout admission and live borrow ownership are independent.
pn tune32_phase2_admitted_store(var values: float[], count: int) {
    var index: int = 0
    while (index < count) {
        values[index] = values[index] + 1.0
        index = index + 1
    }
}

pn tune32_phase2_admitted_read(values: float[], count: int) float {
    var total: float = 0.0
    var index: int = 0
    while (index < count) {
        total = total + values[index]
        index = index + 1
    }
    return total
}

pn main() {
    var values: float[] = [1.0, 2.0, 3.0, 4.0]
    let snapshot = values
    tune32_phase2_admitted_store(values, 4)
    print(tune32_phase2_admitted_read(values, 4) == 14.0); print("\n")
    print(tune32_phase2_admitted_read(snapshot, 4) == 10.0); print("\n")
    tune32_phase2_admitted_store(values, 4)
    print(tune32_phase2_admitted_read(values, 4) == 18.0); print("\n")
    tune32_phase2_admitted_store(values, 0)
    tune32_phase2_admitted_store(values, -1)
    print(tune32_phase2_admitted_read(values, 4) == 18.0); print("\n")
    var empty: float[] = fill(0, 0.0)
    tune32_phase2_admitted_store(empty, 0)
    print(len(empty)); print("\n")
    print(values[4] == null); print("\n")
}
