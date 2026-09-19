// Tune31 T31-2 / D3.2.1-D3.3.1: an open outer builder may retain only
// proven int-lane returns; indexing it is admitted at the consumer boundary.

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
    var rows = []
    rows.push(build_codes("AB"))
    print(sum_codes(rows[0]) ++ "\n")
}
