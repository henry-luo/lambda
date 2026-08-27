// Tune15 B3: a statically-conforming scalar write on a fixed-shape record
// uses the packed field slot rather than the validator/COW setter.

type Tune15State = {moves: int}

pn tune15_record_step(var st: Tune15State) {
    st.moves = st.moves + 1
}

pn tune15_record() int {
    var st: Tune15State = {moves: 0}
    tune15_record_step(st)
    return st.moves
}

pn main() {
    print(string(tune15_record()) ++ "\n")
}
