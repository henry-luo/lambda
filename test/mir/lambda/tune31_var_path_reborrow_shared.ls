// Tune31 Phase II A: sharing a `var` root inside the body must restore root
// preparation before a later nested `var` field borrow.

type Counter = {values: int[]}

pn tune31_shared_increment(var values: int[]) any {
    values[0] = values[0] + 1
}

pn tune31_share_then_increment(var counter: Counter) any {
    let snapshot = counter
    tune31_shared_increment(counter.values)
    print(counter.values[0]); print(" ")
    print(snapshot.values[0]); print("\n")
}

pn main() {
    var counter: Counter = {values: [0]}
    tune31_share_then_increment(counter)
    print(counter.values[0]); print("\n")
}
