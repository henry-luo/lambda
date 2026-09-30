// D2.6.5v3/S9.1.2: a unique open array appends string Items in place;
// growth, a shared snapshot, and a non-string item retain the generic path.
pn main() {
    var words: array = []
    var index = 0
    while (index < 24) {
        push(words, "x")
        index = index + 1
    }
    let snapshot = words
    push(words, "y")
    push(words, ["a", "b"])
    print([len(snapshot), len(words), words[0], words[24], words[25], words[26]])
    print("\n")
    // The admitted string[] read still needs its live tag guard at an
    // out-of-range index, where the successful-element contract yields null.
    var typed: string[] = ["q"]
    push(words, typed[0])
    push(words, typed[2])
    print([len(words), words[26], words[27]])
    print("\n")
}
