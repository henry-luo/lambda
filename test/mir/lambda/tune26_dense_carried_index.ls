// Tune26 / D2.3.4-D2.3.5 / D4.3.3: a strict previous-index recurrence
// shares one dense extent proof across a complete float expression tree.

pn tune26_dense_carried_index(values: float[], n: int) float^ {
    var previous_index: int = n - 1
    var i: int = 0
    var total: float = 0.0
    while (i < n) {
        let _admitted_total_9: float = total + values[previous_index] * 2.0 + values[i]
        total = _admitted_total_9^
        previous_index = i
        i = i + 1
    }
    return total
}

pn main() {
    var rejected = false
    let total = tune26_dense_carried_index([1.0, 2.0, 4.0, 8.0], 4)
    tune26_dense_carried_index([1.0], 2) ^ { rejected = true }
    print([total, rejected])
}
