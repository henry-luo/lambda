// Tune31 Phase II F / S9.1.3: a `var` inferred ArrayNum parameter keeps its
// caller-visible COW home while a dynamically indexed float store uses its
// guarded native lane.

pn update_values(var values, sources, n) {
    var i = 0
    while (i < n) {
        var delta = values[i] - sources[i]
        var magnitude = delta * 0.5
        values[i] = values[i] - magnitude
        i = i + 1
    }
}

pn main() {
    var values = [3.0, 4.0, 5.0]
    var sources = [1.0, 2.0, 3.0]
    let snapshot = values
    update_values(values, sources, 3)
    print(values); print(" ")
    print(snapshot); print("\n")
}
