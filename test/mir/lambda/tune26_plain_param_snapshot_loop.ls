// Tune26 T26-4: a mutated plain float[] parameter detaches at entry, retaining
// S9.1.3 snapshot semantics while its same-lane loop uses the unique owner.

pn tune26_plain_param_snapshot_loop(values: float[], n: int) float {
    var i: int = 0
    while (i < n) {
        values[i] = values[i] + 0.5
        i = i + 1
    }
    return values[0] + values[n - 1]
}

pn main() {
    var values: float[] = [1.0, 2.0, 3.0]
    let snapshot = values
    print(floor(tune26_plain_param_snapshot_loop(values, 3))); print(" ")
    print(floor(values[0] + values[2])); print(" ")
    print(floor(snapshot[0] + snapshot[2])); print("\n")
}
