// Tune31 T31-2 / D4.2.1-D4.3.1: a homogeneous append builder returns
// an int lane, and callers consume that lane without generic indexing.

pn build_codes(text) {
    var codes = []
    var index = 0
    while (index < len(text)) {
        codes.push(ord(text[index]))^
        index = index + 1
    }
    codes
}

pn sum_codes(codes) {
    var total = 0
    var index = 0
    while (index < len(codes)) {
        total = total + codes[index]
        index = index + 1
    }
    total
}

// nullable and mixed append domains keep ordinary open construction.
pn nullable_codes(text: string) {
    var codes = []
    codes.push(ord(text[99]))^
    codes
}

pn mixed_codes() {
    var codes = []
    codes.push(1)^
    codes.push("x")^
    codes
}

pn main() {
    print(sum_codes(build_codes("AB")) ++ "\n")
    var escaped = build_codes("AB")
    let snapshot = escaped
    escaped[0] = "x"
    print([snapshot, escaped, nullable_codes("AB"), mixed_codes()])
    print("\n")
}
