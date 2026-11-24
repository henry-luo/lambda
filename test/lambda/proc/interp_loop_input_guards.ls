// D8.3.2: the first call can specialize the loop's live input as int.
// A later call reaches the same loop head with a different value domain.
pn carry(seed) {
    var value = seed
    var i = 0
    while (i < 4) {
        if (i == 3) { return value }
        i = i + 1
    }
    return value
}

pn replace_inputs() {
    var value = 8
    var data = fill(2, 0)
    let alias = data
    var i = 0
    while (i < 4) {
        if (i == 2) {
            // S12.2.1v3: entry observations cannot constrain later stores.
            value = "text"
            data[0] = value
        }
        i = i + 1
    }
    // S9.1.2: the previously shared array keeps its original element.
    print(alias[0]); print(" "); print(data[0]); print(" "); print(value)
}

pn main() {
    print(carry(7)); print(" "); print(carry("word")); print("\n")
    replace_inputs()^
    print("\n")
}
