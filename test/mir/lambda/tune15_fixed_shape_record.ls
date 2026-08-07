// Tune15 B3: a statically-conforming scalar write on a fixed-shape record
// uses the packed field slot rather than the validator/COW setter.

type Tune15State = {moves: int}

pn tune15_record_step(var state: Tune15State) {
    state.moves = state.moves + 1
}

pn tune15_record() int {
    var state: Tune15State = {moves: 0}
    tune15_record_step(state)
    return state.moves
}

pn main() {
    print(string(tune15_record()) ++ "\n")
}
