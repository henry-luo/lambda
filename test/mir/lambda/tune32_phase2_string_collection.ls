// D3.3.3v3: element candidates never certify a collection's physical slots.
pn tune32_phase2_words() {
    var words = []
    words.push("al" ++ "pha")
    words.push("é")
    words
}

fn tune32_phase2_word(words, index) => if (index < len(words)) words[index] else ""
fn tune32_phase2_forward(words, index) => tune32_phase2_word(words, index)
fn tune32_phase2_string_compare(words, index) => tune32_phase2_word(words, index) == tune32_phase2_word(words, 0)

pn tune32_phase2_source() { print("R"); tune32_phase2_words() }
pn tune32_phase2_key() { print("K"); 0 }

pn tune32_phase2_mixed_words(mode) {
    var words = []
    words.push("alpha")
    if (mode) { return words }
    words.push(7)
    return words
}

pn tune32_phase2_recursive_words(n) {
    if (n == 0) { return tune32_phase2_words() }
    return tune32_phase2_recursive_words(n - 1)
}

pn main() {
    let words = tune32_phase2_words()
    print(tune32_phase2_forward(words, 0)); print("\n")
    print(tune32_phase2_forward(words, 1)); print("\n")
    print(tune32_phase2_forward(words, -1)); print("\n")
    print(tune32_phase2_forward(words, 2)); print("\n")
    let reader = tune32_phase2_word
    print(reader(["mixed", 7], 1)); print("\n")
    print(reader(null, 0)); print("\n")
    print(reader([null, "end"], 0)); print("\n")
    print(tune32_phase2_word(tune32_phase2_source(), tune32_phase2_key())); print("\n")
    print(tune32_phase2_word(words, 9007199254740991)); print("\n")
    let captured = (i) => words[i]
    print(captured(1)); print("\n")
    var segments = split("é alpha", " ")
    print(tune32_phase2_word(segments, 0)); print("\n")
    let snapshot = segments
    segments[1] = 42
    print(reader(segments, 1)); print("\n")
    print(reader(snapshot, 1)); print("\n")
    print(tune32_phase2_word([error("selected")], 0) is error); print("\n")
    print(reader(tune32_phase2_mixed_words(false), 1)); print("\n")
    print(reader(tune32_phase2_mixed_words(true), 1)); print("\n")
    print(reader(tune32_phase2_recursive_words(3), 1)); print("\n")
    var changed = tune32_phase2_words()
    let old_words = changed
    changed[0] = 9
    print(reader(changed, 0)); print("\n")
    print(reader(old_words, 0)); print("\n")
    print(tune32_phase2_string_compare(words, 0)); print("\n")
    print(tune32_phase2_string_compare(words, -1)); print("\n")
    print(tune32_phase2_string_compare(words, 2)); print("\n")
    let compare = tune32_phase2_string_compare
    print(compare([7], 0)); print("\n")
    print(compare([null], 0)); print("\n")
    print(compare([error("comparison")], 0)); print("\n")
}
