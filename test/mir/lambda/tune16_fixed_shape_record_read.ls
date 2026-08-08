// Tune16 C3: fixed-shape record reads use the packed field offset.

type Tune16State = {moves: int, label: string}

pn tune16_record_read(state: Tune16State) int {
    return state.moves
}

pn main() {
    var state: Tune16State = {moves: 41, label: "ok"}
    print(tune16_record_read(state) + 1)
    print("\n")
}
