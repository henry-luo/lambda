// Tune31 Phase II A: a `var` record root is exclusive on entry. Re-borrowing
// its typed array field keeps child COW while avoiding a root no-op per call.

type Counter = {values: int[]}

pn tune31_increment(var values: int[], index: int) any {
    values[index] = values[index] + 1
}

pn tune31_repeat(var counter: Counter, rounds: int) any {
    var i: int = 0
    while (i < rounds) {
        tune31_increment(counter.values, 0)
        i = i + 1
    }
}

pn main() {
    var counter: Counter = {values: [0]}
    let snapshot = counter
    tune31_repeat(counter, 4)
    print(counter.values[0]); print(" ")
    print(snapshot.values[0]); print("\n")
}
