// Tune31 Phase II F / D3.3.3v3: inference can retain the live ArrayNum
// witness for an unannotated parameter without turning it into a T[] contract.

pn update_first(values, n) {
    var i = 0
    while (i < n) {
        values[0] = values[0] + 0.5
        i = i + 1
    }
    return values[0]
}

pn main() {
    var values = [1.0, 2.0]
    let snapshot = values
    print(update_first(values, 8)); print(" ")
    print(values[0]); print(" ")
    print(snapshot[0]); print("\n")
}
