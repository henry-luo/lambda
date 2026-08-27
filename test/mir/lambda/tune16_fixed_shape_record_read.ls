// Tune16 C3: fixed-shape record reads use the packed field offset.

type Tune16State = {moves: int, label: string}

pn tune16_record_read(st: Tune16State) int {
    return st.moves
}

pn main() {
    var st: Tune16State = {moves: 41, label: "ok"}
    print(tune16_record_read(st) + 1)
    print("\n")
}
