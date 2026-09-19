// Tune31 T31-2 / D3.2.1-D3.3.1: a homogeneous append builder returns
// an int lane, and callers consume that lane without generic indexing.

pn build_codes(text) {
    var codes = []
    var index = 0
    while (index < len(text)) {
        codes.push(ord(text[index]))
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

pn main() {
    print(sum_codes(build_codes("AB")) ++ "\n")
}
