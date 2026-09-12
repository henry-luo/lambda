// Tune26 T26-4: a sharing effect late in a loop iteration must protect an
// earlier typed var call when execution returns over the backedge.

pn tune26_loop_cow_add(var values: float[], addends: float[]) any {
    values[0] = values[0] + addends[0]
}

pn tune26_loop_cow_snapshot(values: float[]) any {
    values[0] = values[0]
}

pn main() {
    var values: float[] = [0.0]
    let addends: float[] = [1.0]
    var iteration: int = 0
    while (iteration < 2) {
        tune26_loop_cow_add(values, addends)
        tune26_loop_cow_snapshot(values)
        iteration = iteration + 1
    }
    print(values[0]); print("\n")
}
