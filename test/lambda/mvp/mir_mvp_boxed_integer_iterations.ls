// D7.4.1/S7.7.2v2: a guard miss resumes at the current checked-loop state.
pn transform(value: int, initial_shift: int, stop: int) any {
    var current = shr(value, initial_shift)
    var rounds = 0
    while (rounds < 8) {
        if (rounds == stop) { break }
        current = shr(current * 3 + 1, 1)
        rounds = rounds + 1
    }
    return [current, rounds]
}
pn main() {
    print(transform(64, 1, 4)); print("\n")
    print(transform(64, 1, 8)); print("\n")
    print(transform(1125899906842624, 1, 4)); print("\n")
    print(transform(2251799813685248, 1, 0)); print("\n")
    print(transform(7, -1, 0)[0] is error); print("\n")
}
