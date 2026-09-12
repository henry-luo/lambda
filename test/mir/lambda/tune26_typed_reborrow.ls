// Tune26 T26-4: an exclusive typed `var` re-borrow needs no repeated COW
// prepare; a snapshot at the outer call boundary remains isolated.

pn tune26_typed_reborrow_step(var values: int[], n: int) any {
    var i: int = 0
    while (i < n) {
        values[i] = values[i] + 1
        i = i + 1
    }
}

pn tune26_typed_reborrow_loop(var values: int[], rounds: int) any {
    var iteration: int = 0
    while (iteration < rounds) {
        tune26_typed_reborrow_step(values, 4)
        iteration = iteration + 1
    }
}

pn main() {
    var values: int[] = [0, 0, 0, 0]
    let snapshot = values
    tune26_typed_reborrow_loop(values, 3)
    print(values[0]); print(" ")
    print(values[3]); print(" ")
    print(snapshot[0]); print("\n")
}
